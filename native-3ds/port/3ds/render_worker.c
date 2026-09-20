#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "render_worker.h"
#include "render_snapshot.h"

/* One producer, one consumer. Synchronous mode tests ownership separately;
 * async mode queues immutable draws, with a CPU-consumption fence at End.
 * Native linear buffers keep their independent existing GPU lifetime. */
enum { JOB_BEGIN, JOB_END, JOB_DRAW, JOB_EFB, JOB_DIRTY, JOB_INVALIDATE,
       JOB_VISIBILITY, JOB_COUNTS, JOB_BENCHMARK };
#define JOB_CAPACITY 256
#define ARENA_CAPACITY (4*1024*1024)
#define SOURCE_CAPACITY (4*1024*1024)
typedef struct {
    unsigned kind, a, b, c;
    const void *input, *state;
    unsigned *out_a, *out_b;
    const MPDrawSnapshot *snapshot;
} RenderJob;
static Thread worker;
static LightEvent wake, done;
static RenderJob jobs[JOB_CAPACITY];
static unsigned published, completed, failed, stopping;
/* Producer-owned acknowledgement avoids synchronization/timing syscalls on
 * the original-path baseline after the last CPU fence has already joined. */
static unsigned acknowledged;
static MPSnapshotArena arena;
static MPSourceSnapshotCache source_cache;
static const MPDrawSnapshot *active_snapshot;
static struct { unsigned address,bytes; } copied_destinations[32];
static unsigned copied_destination_count, copied_destination_overflow;
static char failure_message[256];
#ifdef MP_SMOKE_TEST
volatile unsigned mp_render_worker_disable;
volatile unsigned mp_render_worker_async;
volatile unsigned mp_render_worker_source_cache_disable;
volatile unsigned mp_render_worker_source_cache_validate;
volatile unsigned mp_render_worker_geometry_borrow;
volatile unsigned mp_render_worker_test_failure;
#else
#define mp_render_worker_disable 0
#define mp_render_worker_async 1
#define mp_render_worker_source_cache_disable 0
#define mp_render_worker_source_cache_validate 0
#define mp_render_worker_geometry_borrow 1
#endif
unsigned mp_render_worker_geometry_contract,mp_render_worker_borrowed_draws;
unsigned mp_render_worker_geometry_retirements;
unsigned mp_render_worker_geometry_busy_retirements;
u64 mp_render_worker_borrowed_bytes;
extern const unsigned mp_geometry_borrow_contract __attribute__((weak));
unsigned mp_render_worker_active, mp_render_worker_core = ~0u;
unsigned mp_render_worker_jobs, mp_render_worker_fallbacks, mp_render_worker_failures;
u64 mp_render_worker_wait_ticks, mp_render_worker_busy_ticks;
unsigned mp_render_worker_snapshots, mp_render_worker_arena_flushes, mp_render_worker_snapshot_fallbacks;
unsigned mp_render_worker_queue_peak, mp_render_worker_arena_peak;
u64 mp_render_worker_copy_ticks;
unsigned mp_render_worker_source_hits,mp_render_worker_source_checks,mp_render_worker_source_mismatches;
unsigned mp_render_worker_source_bytes,mp_render_worker_source_evictions;
u64 mp_render_worker_source_copied_bytes,mp_render_worker_source_hit_bytes;

extern void mp_renderer_impl_begin(void), mp_renderer_impl_end(void), mp_renderer_impl_exit(void);
extern void mp_renderer_impl_submit(const void*, unsigned, const void*);
extern void mp_renderer_impl_efb_copy(const u32*);
extern void mp_renderer_impl_texture_dirty(unsigned, unsigned, unsigned);
extern void mp_renderer_impl_texture_invalidate(void), mp_renderer_impl_frame_texture_visibility(void);
extern void mp_renderer_impl_counts(unsigned*, unsigned*);
extern void mp_renderer_impl_benchmark_frame(unsigned);
extern void mp_native_log(const char*);
extern void mp_native_panic(const char*) __attribute__((noreturn));

const void *mp_render_worker_texture_source(unsigned address, unsigned bytes) {
    if(!active_snapshot || !bytes) return (const void*)(uintptr_t)address;
    const void *data=mp_snapshot_source(active_snapshot,address,bytes);
    if(!data) mp_native_panic("Queued draw has no immutable texture source");
    return data;
}
static uint32_t be_pointer(const void *pointer) {
    return __builtin_bswap32((uint32_t)(uintptr_t)pointer);
}
static void draw_snapshot(const MPDrawSnapshot *s) {
    uint32_t draw[MP_DRAW_WORDS], layer[sizeof(MPTextureLayer)/4];
    memcpy(draw,s->draw,sizeof(draw));
    draw[MP_DRAW_GPU]=be_pointer(s->uniforms);
    draw[MP_DRAW_INDICES]=be_pointer(s->indices);
    if(s->layer) {
        memcpy(layer,s->layer,sizeof(layer));
        if(s->uv) layer[9]=be_pointer(s->uv);
        draw[MP_DRAW_LAYER]=be_pointer(layer);
    }
    active_snapshot=s;
    mp_renderer_impl_submit(s->vertices,s->count,draw);
    active_snapshot=NULL;
}

static void execute(const RenderJob *q) {
    switch(q->kind) {
    case JOB_BEGIN: mp_renderer_impl_begin(); break;
    case JOB_END: mp_renderer_impl_end(); break;
    case JOB_DRAW:
        if(q->snapshot) draw_snapshot(q->snapshot);
        else mp_renderer_impl_submit(q->input, q->a, q->state);
        break;
    case JOB_EFB: mp_renderer_impl_efb_copy(q->input); break;
    case JOB_DIRTY: mp_renderer_impl_texture_dirty(q->a, q->b, q->c); break;
    case JOB_INVALIDATE: mp_renderer_impl_texture_invalidate(); break;
    case JOB_VISIBILITY: mp_renderer_impl_frame_texture_visibility(); break;
    case JOB_COUNTS: mp_renderer_impl_counts(q->out_a, q->out_b); break;
#ifdef MP_SMOKE_TEST
    case JOB_BENCHMARK: mp_renderer_impl_benchmark_frame(q->a); break;
#endif
    }
}

void mp_render_worker_panic(const char *message) {
    if(!worker || threadGetCurrent()!=worker) return;
    /* Main only reads this buffer after acquiring failed. No formatting,
     * logging or renderer recursion is required on the failing worker. */
    unsigned n=0;
    while(n+1<sizeof(failure_message) && message[n]) {
        failure_message[n]=message[n]; ++n;
    }
    failure_message[n]=0;
    ++mp_render_worker_failures;
    __atomic_store_n(&failed, 1, __ATOMIC_RELEASE);
    LightEvent_Signal(&done);
    threadExit(1);
}

static void render_thread(void *unused) {
    (void)unused;
    mp_render_worker_core=svcGetProcessorID();
    for(;;) {
        if(__atomic_load_n(&stopping, __ATOMIC_ACQUIRE)) break;
        unsigned cursor=__atomic_load_n(&completed,__ATOMIC_RELAXED);
        if(cursor==__atomic_load_n(&published,__ATOMIC_ACQUIRE)) {
            LightEvent_Wait(&wake); continue;
        }
#ifdef MP_SMOKE_TEST
        if(mp_render_worker_test_failure) {
            mp_render_worker_test_failure=0;
            mp_native_panic("Injected renderer worker failure");
        }
#endif
        u64 start=svcGetSystemTick();
        execute(&jobs[cursor%JOB_CAPACITY]);
        mp_render_worker_busy_ticks+=svcGetSystemTick()-start;
        ++mp_render_worker_jobs;
        __atomic_store_n(&completed, cursor+1, __ATOMIC_RELEASE);
        LightEvent_Signal(&done);
    }
}

void mp_render_worker_start(int is_new) {
    if(worker || !is_new) return;
    published=completed=failed=stopping=acknowledged=0; failure_message[0]=0;
    if(!arena.data) { arena.data=malloc(ARENA_CAPACITY); arena.capacity=arena.data?ARENA_CAPACITY:0; }
    if(!arena.data){mp_native_log("Renderer packet allocation unavailable; using original synchronous renderer\n");return;}
    arena.used=arena.sources=0; active_snapshot=NULL;
    source_cache.budget=SOURCE_CAPACITY;
    mp_render_worker_geometry_contract=&mp_geometry_borrow_contract && __builtin_bswap32(mp_geometry_borrow_contract)==1;
    LightEvent_Init(&wake, RESET_ONESHOT);
    LightEvent_Init(&done, RESET_ONESHOT);
    s32 priority=0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    /* Explicit core 2 uses the existing New3DS capability in the CIA and
     * Luma homebrew exheaders. Failure retains the original renderer. */
    worker=threadCreate(render_thread, NULL, 128*1024, priority, 2, false);
    mp_render_worker_active=worker!=NULL;
    mp_native_log(worker ? "Renderer CPU worker created with explicit core-2 affinity; immutable draw packets\n"
                          : "Renderer core 2 unavailable; using original synchronous renderer\n");
}

void mp_render_worker_stop(void) {
    if(!worker) return;
    if(!__atomic_load_n(&failed,__ATOMIC_ACQUIRE)) mp_render_worker_barrier();
    __atomic_store_n(&stopping, 1, __ATOMIC_RELEASE);
    LightEvent_Signal(&wake);
    threadJoin(worker, U64_MAX);
    threadFree(worker); worker=NULL;
    mp_render_worker_active=0;
    active_snapshot=NULL;
    arena.used=arena.sources=0;
    mp_source_cache_release(&source_cache);
}

static void check_failure(void) {
    if(!__atomic_load_n(&failed,__ATOMIC_ACQUIRE)) return;
    /* Join before entering the main thread's original panic/cleanup path. */
    mp_render_worker_stop();
    failed=0;
    mp_native_panic(failure_message);
}
static void wait_until(unsigned target) {
    if(!worker || acknowledged==target) return;
    check_failure();
    if(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)==target) { acknowledged=target; return; }
    u64 start=svcGetSystemTick();
    for(;;) {
        check_failure();
        if(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)==target) break;
        LightEvent_Wait(&done);
    }
    mp_render_worker_wait_ticks+=svcGetSystemTick()-start;
    acknowledged=target;
}
void mp_render_worker_barrier(void) {
    wait_until(__atomic_load_n(&published,__ATOMIC_RELAXED));
}
void mp_render_worker_retire_geometry(void) {
    ++mp_render_worker_geometry_retirements;
    if(worker && __atomic_load_n(&published,__ATOMIC_RELAXED)!=__atomic_load_n(&completed,__ATOMIC_ACQUIRE))
        ++mp_render_worker_geometry_busy_retirements;
    mp_render_worker_barrier();
}
static int asynchronous(void) {
    return worker && !mp_render_worker_disable && mp_render_worker_async;
}
static void dispatch(RenderJob q,int async) {
    if(!worker || mp_render_worker_disable) {
        mp_render_worker_barrier();
        ++mp_render_worker_fallbacks; execute(&q); return;
    }
    check_failure();
    unsigned cursor=__atomic_load_n(&published,__ATOMIC_RELAXED);
    unsigned used=cursor-__atomic_load_n(&completed,__ATOMIC_ACQUIRE);
    if(used==JOB_CAPACITY) {
        /* Bound memory and apply backpressure without dropping any draw. */
        wait_until(cursor); used=0;
    }
    if(used+1>mp_render_worker_queue_peak) mp_render_worker_queue_peak=used+1;
    jobs[cursor%JOB_CAPACITY]=q;
    __atomic_store_n(&published, cursor+1, __ATOMIC_RELEASE);
    LightEvent_Signal(&wake);
    if(!async) wait_until(cursor+1);
}

void mp_renderer_begin(void) {
    dispatch((RenderJob){.kind=JOB_BEGIN},0);
    copied_destination_count=copied_destination_overflow=0;
}
void mp_renderer_end(void) {
    dispatch((RenderJob){.kind=JOB_END},0);
    arena.used=arena.sources=0;
    mp_source_cache_release(&source_cache);
    mp_render_worker_source_hits=source_cache.hits;mp_render_worker_source_checks=source_cache.checks;
    mp_render_worker_source_mismatches=source_cache.mismatches;mp_render_worker_source_bytes=source_cache.bytes;
    mp_render_worker_source_evictions=source_cache.evictions;
    mp_render_worker_source_copied_bytes=source_cache.copied_bytes;mp_render_worker_source_hit_bytes=source_cache.hit_bytes;
}
static const void *resolve_input(uint32_t address,size_t bytes,void *ctx) {
    (void)ctx;
    /* A GPU-only EFB copy may use a key with no full CPU image behind it.
     * Such draws keep the existing synchronized cache path; do not try to
     * snapshot bytes that the original renderer would never read. Tracking
     * all copy destinations is conservative, including CPU-visible copies. */
    for(unsigned i=0;i<copied_destination_count;++i)
        if(mp_texture_range_overlap(address,(unsigned)bytes,copied_destinations[i].address,copied_destinations[i].bytes)) return NULL;
    return (const void*)(uintptr_t)address;
}
void mp_native_submit(const void *vertices, unsigned count, const void *draw) {
    if(asynchronous() && arena.data && !copied_destination_overflow) {
        arena.persistent=mp_render_worker_source_cache_disable?NULL:&source_cache;
        arena.borrow_geometry=mp_render_worker_geometry_borrow && mp_render_worker_geometry_contract;
        source_cache.validate=mp_render_worker_source_cache_validate;
        u64 start=svcGetSystemTick();
        MPDrawSnapshot *s=mp_snapshot_draw(&arena,vertices,count,draw,resolve_input,NULL);
        mp_render_worker_copy_ticks+=svcGetSystemTick()-start;
        if(!s) {
            mp_render_worker_barrier(); arena.used=arena.sources=0;
            mp_source_cache_release(&source_cache);
            ++mp_render_worker_arena_flushes;
            start=svcGetSystemTick();
            s=mp_snapshot_draw(&arena,vertices,count,draw,resolve_input,NULL);
            mp_render_worker_copy_ticks+=svcGetSystemTick()-start;
        }
        if(source_cache.mismatches) mp_native_panic("Immutable texture snapshot differs from current engine source");
        if(s) {
            if(s->borrowed_geometry) {
                ++mp_render_worker_borrowed_draws;
                mp_render_worker_borrowed_bytes+=(u64)count*sizeof(MPGPUVertex)+(u64)mp_snapshot_word(s->draw+MP_DRAW_INDEX_COUNT)*2;
            }
            if(arena.used>mp_render_worker_arena_peak) mp_render_worker_arena_peak=arena.used;
            ++mp_render_worker_snapshots;
            dispatch((RenderJob){.kind=JOB_DRAW,.snapshot=s},1); return;
        }
        ++mp_render_worker_snapshot_fallbacks;
    }
    dispatch((RenderJob){.kind=JOB_DRAW, .input=vertices, .a=count, .state=draw},0);
}
void mp_native_efb_copy(const u32 *request) {
    /* Original EFB copy can write CPU memory, clear the framebuffer or create
     * a GPU-only cache image. Preserve all three operations in stream order. */
    dispatch((RenderJob){.kind=JOB_EFB,.input=request},0); arena.sources=0;
    unsigned copied_address=mp_snapshot_word(request);
    unsigned copied_bytes=mp_texture_source_bytes(mp_snapshot_word(request+7),mp_snapshot_word(request+5),mp_snapshot_word(request+6));
    mp_source_cache_dirty(&source_cache,copied_address,copied_bytes,0);
    if(copied_destination_count<32) {
        unsigned i=copied_destination_count++;
        copied_destinations[i].address=copied_address;
        copied_destinations[i].bytes=copied_bytes;
    } else copied_destination_overflow=1;
}
void mp_native_texture_dirty(unsigned address, unsigned bytes, unsigned cpu_written) {
    arena.sources=0;
    mp_source_cache_dirty(&source_cache,address,bytes,0);
    dispatch((RenderJob){.kind=JOB_DIRTY,.a=address,.b=bytes,.c=cpu_written},asynchronous());
}
void mp_native_texture_invalidate(void) {
    arena.sources=0;mp_source_cache_dirty(&source_cache,0,0,1);
    dispatch((RenderJob){.kind=JOB_INVALIDATE},asynchronous());
}
void mp_native_frame_texture_visibility(void) {
    /* This is a frame checkpoint, not a CPU write. Retain persistent source
     * copies across it; dirty/invalidate events carry source changes. */
    arena.sources=0; dispatch((RenderJob){.kind=JOB_VISIBILITY},asynchronous());
}
void mp_renderer_counts(unsigned *vertices, unsigned *draws) {
    dispatch((RenderJob){.kind=JOB_COUNTS,.out_a=vertices,.out_b=draws},0);
}
#ifdef MP_SMOKE_TEST
void mp_renderer_benchmark_frame(unsigned frame) { dispatch((RenderJob){.kind=JOB_BENCHMARK,.a=frame},0); }
#endif
void mp_render_worker_report(unsigned frame){
    if(frame%120)return;
    /* Called on main after End has acknowledged every CPU job. Consumer
     * counters, including 64-bit values, are stable at this boundary. */
    mp_render_worker_barrier();
    static unsigned jobs0,borrow0,retire0,busy0,fallback0;
    static u64 copy0,consume0,wait0,source0,hit0,geometry0;
    char text[480];
    snprintf(text,sizeof(text),"Renderer CPU/120 frames active=%u core=%u queued=%u borrowed=%u copy=%.2f ms consume=%.2f ms wait=%.2f ms source-copy=%llu source-reuse=%llu geometry-reuse=%llu retires=%u busy-retires=%u fallbacks=%u\n",
        mp_render_worker_active,mp_render_worker_core,mp_render_worker_jobs-jobs0,mp_render_worker_borrowed_draws-borrow0,
        (mp_render_worker_copy_ticks-copy0)*1000.0/SYSCLOCK_ARM11/120,
        (mp_render_worker_busy_ticks-consume0)*1000.0/SYSCLOCK_ARM11/120,
        (mp_render_worker_wait_ticks-wait0)*1000.0/SYSCLOCK_ARM11/120,
        (unsigned long long)(mp_render_worker_source_copied_bytes-source0),
        (unsigned long long)(mp_render_worker_source_hit_bytes-hit0),
        (unsigned long long)(mp_render_worker_borrowed_bytes-geometry0),
        mp_render_worker_geometry_retirements-retire0,mp_render_worker_geometry_busy_retirements-busy0,
        mp_render_worker_snapshot_fallbacks-fallback0);
    jobs0=mp_render_worker_jobs;borrow0=mp_render_worker_borrowed_draws;
    retire0=mp_render_worker_geometry_retirements;busy0=mp_render_worker_geometry_busy_retirements;
    fallback0=mp_render_worker_snapshot_fallbacks;
    copy0=mp_render_worker_copy_ticks;consume0=mp_render_worker_busy_ticks;wait0=mp_render_worker_wait_ticks;
    source0=mp_render_worker_source_copied_bytes;hit0=mp_render_worker_source_hit_bytes;geometry0=mp_render_worker_borrowed_bytes;
    mp_native_log(text);
}
void mp_renderer_exit(void) {
    mp_render_worker_stop();
    mp_renderer_impl_exit();
    mp_source_cache_close(&source_cache);
    free(arena.data); arena.data=NULL; arena.capacity=0;
}
