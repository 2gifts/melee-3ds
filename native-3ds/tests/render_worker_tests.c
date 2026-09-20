#include "render_worker_host/3ds.h"
#include <windows.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/3ds/render_worker.h"
#include "../port/engine/gpu_vertex.h"

struct TestThread { HANDLE handle; void (*entry)(void*); void *arg; int core; };
static _Thread_local Thread current;
static int deny_core;
#ifdef MP_TEST_GEOMETRY_CONTRACT
const unsigned mp_geometry_borrow_contract=0x01000000; /* BE8 word 1 */
#endif
extern unsigned mp_render_worker_geometry_contract,mp_render_worker_geometry_retirements;
static HANDLE backpressure_gate;
static int released_gate;
static HANDLE borrowed_wait_gate;
static int released_borrowed_gate;
extern unsigned mp_render_worker_queue_peak;
static DWORD WINAPI entry(LPVOID p) {
    current=p; current->entry(current->arg); return 0;
}
Thread threadCreate(void(*fn)(void*),void*arg,size_t size,int prio,int core,bool detached) {
    (void)size; (void)prio; assert(core==2 && !detached);
    if(deny_core) return NULL;
    Thread t=calloc(1,sizeof(*t)); assert(t);
    t->entry=fn; t->arg=arg; t->core=core;
    t->handle=CreateThread(NULL,0,entry,t,0,NULL); assert(t->handle); return t;
}
Thread threadGetCurrent(void) { return current; }
void threadJoin(Thread t,u64 timeout) {
    assert(timeout==U64_MAX); assert(WaitForSingleObject(t->handle,10000)==WAIT_OBJECT_0);
}
void threadFree(Thread t) { CloseHandle(t->handle); free(t); }
void threadExit(int code) { ExitThread(code); __builtin_unreachable(); }
void LightEvent_Init(LightEvent *e,int reset) {
    assert(reset==RESET_ONESHOT);
    if(e->event) CloseHandle(e->event);
    e->event=CreateEvent(NULL,FALSE,FALSE,NULL); assert(e->event);
}
void LightEvent_Wait(LightEvent *e) {
    if(!current&&borrowed_wait_gate&&!released_borrowed_gate){released_borrowed_gate=1;SetEvent(borrowed_wait_gate);}
    if(!current && backpressure_gate && !released_gate) {
        /* The consumer is deliberately stalled in the first queued job.
         * The producer must stop at capacity, then resume without drops. */
        assert(mp_render_worker_queue_peak==256);
        released_gate=1; SetEvent(backpressure_gate);
    }
    assert(WaitForSingleObject(e->event,10000)==WAIT_OBJECT_0);
}
void LightEvent_Signal(LightEvent *e) { assert(SetEvent(e->event)); }
u64 svcGetSystemTick(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
u64 testTickFrequency(void) { LARGE_INTEGER t; QueryPerformanceFrequency(&t); return t.QuadPart; }
s32 svcGetProcessorID(void) { return current?current->core:0; }
void svcGetThreadPriority(s32 *priority,int handle) { (void)handle; *priority=0x30; }

extern void mp_renderer_begin(void),mp_renderer_end(void),mp_renderer_exit(void);
extern void mp_native_submit(const void*,unsigned,const void*),mp_native_efb_copy(const u32*);
extern void mp_native_texture_dirty(unsigned,unsigned,unsigned);
extern void mp_native_texture_invalidate(void),mp_native_frame_texture_visibility(void);
extern void mp_renderer_counts(unsigned*,unsigned*),mp_renderer_benchmark_frame(unsigned);
extern volatile unsigned mp_render_worker_disable,mp_render_worker_test_failure,mp_render_worker_async;
extern unsigned mp_render_worker_active,mp_render_worker_core,mp_render_worker_jobs,mp_render_worker_failures;
static unsigned expected,observed,draws,expected_core;
static unsigned async_test,async_seen;
static volatile LONG inside;
static jmp_buf recover;
static char error[256];

static void check(unsigned kind) {
    assert(InterlockedIncrement(&inside)==1);
    assert(kind==expected);
    assert((unsigned)svcGetProcessorID()==expected_core);
    ++observed;
    assert(InterlockedDecrement(&inside)==0);
}
void mp_renderer_impl_begin(void) { check(0); }
void mp_renderer_impl_end(void) { check(1); }
void mp_renderer_impl_submit(const void *p,unsigned n,const void *state) {
    const unsigned *values=p;
    assert(n==64 && *(const unsigned*)state==draws);
    for(unsigned i=0;i<n;++i) assert(values[i]==draws*101+i);
    check(2); ++draws;
}
void mp_renderer_impl_efb_copy(const u32 *p) { assert(p[0]==0x12345678); check(3); }
void mp_renderer_impl_texture_dirty(unsigned a,unsigned b,unsigned c) {
    if(async_test) {
        if(!async_seen && backpressure_gate) assert(WaitForSingleObject(backpressure_gate,10000)==WAIT_OBJECT_0);
        assert(a==async_seen++ && b==102 && c==103);
    }
    else assert(a==101 && b==102 && c==103);
    check(4);
}
void mp_renderer_impl_texture_invalidate(void) { check(5); }
void mp_renderer_impl_frame_texture_visibility(void) { check(6); }
void mp_renderer_impl_counts(unsigned *v,unsigned *d) { *v=64*draws; *d=draws; check(7); }
void mp_renderer_impl_benchmark_frame(unsigned n) { assert(n==99); check(8); }
void mp_renderer_impl_exit(void) { assert(!threadGetCurrent() && !mp_render_worker_active); }
void mp_native_log(const char *text) { assert(text && !threadGetCurrent()); }
void mp_native_panic(const char *text) {
    mp_render_worker_panic(text);
    assert(!threadGetCurrent() && !mp_render_worker_active);
    snprintf(error,sizeof(error),"%s",text); longjmp(recover,1);
}
static void roundtrip(void) {
    unsigned v[64],tag=draws,out_v,out_d;
    for(unsigned i=0;i<64;++i) v[i]=draws*101+i;
    expected=0; mp_renderer_begin();
    expected=2; mp_native_submit(v,64,&tag);
    memset(v,0xcc,sizeof(v)); tag=~0u; /* Borrowed inputs may now be reused. */
    expected=3; const u32 copy[14]={0x12345678}; mp_native_efb_copy(copy);
    expected=4; mp_native_texture_dirty(101,102,103);
    expected=5; mp_native_texture_invalidate();
    expected=6; mp_native_frame_texture_visibility();
    expected=7; mp_renderer_counts(&out_v,&out_d); assert(out_v==64*draws && out_d==draws);
    expected=8; mp_renderer_benchmark_frame(99);
    expected=1; mp_renderer_end();
}
int main(void) {
    mp_render_worker_start(0); assert(!mp_render_worker_active); roundtrip();
    deny_core=1; mp_render_worker_start(1); assert(!mp_render_worker_active); roundtrip();
    deny_core=0; mp_render_worker_start(1); assert(mp_render_worker_active);
    expected_core=2;
    for(unsigned i=0;i<4096;++i) roundtrip();
    assert(mp_render_worker_core==2 && mp_render_worker_jobs==4096*9);
#ifdef MP_TEST_GEOMETRY_CONTRACT
    assert(mp_render_worker_geometry_contract==1);
#else
    assert(mp_render_worker_geometry_contract==0);
#endif
    mp_render_worker_disable=1; expected_core=0; roundtrip();
    mp_render_worker_disable=0; expected_core=2; roundtrip();
    mp_render_worker_async=1; async_test=1; expected=4;
    backpressure_gate=CreateEvent(NULL,TRUE,FALSE,NULL); assert(backpressure_gate);
    for(unsigned i=0;i<16384;++i) mp_native_texture_dirty(i,102,103);
    mp_render_worker_retire_geometry(); assert(async_seen==16384);
    assert(mp_render_worker_geometry_retirements==1);
    assert(released_gate && mp_render_worker_queue_peak==256);
    CloseHandle(backpressure_gate); backpressure_gate=NULL;
    async_test=0; mp_render_worker_async=0;
    mp_render_worker_test_failure=1;
    if(!setjmp(recover)) { mp_renderer_begin(); assert(!"Failed job returned normally"); }
    assert(!strcmp(error,"Injected renderer worker failure"));
    assert(mp_render_worker_failures==1 && !mp_render_worker_active);
    expected_core=0; roundtrip();
    mp_render_worker_start(1); expected_core=2; roundtrip();
    /* Force a failure with outstanding async work. The producer must wake
     * whether it is filling the ring or waiting on its explicit fence. */
    mp_render_worker_async=1; async_test=1; async_seen=0; expected=4;
    mp_render_worker_test_failure=1;
    if(!setjmp(recover)) {
        for(unsigned i=0;i<4096;++i) mp_native_texture_dirty(i,102,103);
        mp_render_worker_barrier(); assert(!"Async failure lost its wakeup");
    }
    assert(mp_render_worker_failures==2 && !mp_render_worker_active);
    mp_render_worker_start(1); async_seen=0;
    for(unsigned i=0;i<1024;++i) mp_native_texture_dirty(i,102,103);
    mp_render_worker_stop(); assert(async_seen==1024 && !mp_render_worker_active);
    async_test=0; mp_render_worker_async=0;
    mp_renderer_exit(); mp_render_worker_stop();
    printf("Passed %u ordered operations: core-2 ownership, borrowed input lifetime, 16384 async ordered events, bounded backpressure, output acknowledgement, unavailable-core fallback, disable/re-enable, synchronous/async failure wake/join, restart and shutdown.\n",observed);
}
