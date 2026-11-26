/* The Group list's Mii heads: the bottom screen asks for a game host's icon (mp_group_mii_icon, its thread), and the
 * renderer draws it with cfl-mii a step at a time, inside its own frames, so nothing ever waits for a head.
 *
 * Three threads:
 * - the bottom screen asks (mp_group_mii_icon): cached pixels, or one queued job (the latest asked wins);
 * - the renderer's thread (the one that owns citro3d) runs one step of the head in progress per frame: before its
 *   C3D_FrameBegin (mp_mii_icon_between: free what the GPU is done with, copy a finished icon back, start the next
 *   head) and right after it (mp_mii_icon_step: the face bake, the mask bake, the other parts, the icon pass, each
 *   drawn into the renderer's frame with no waits of its own);
 * - a low-priority I/O thread does every file and database job: loading cfl-mii's resource (CFL_Res.dat, about 1 MB,
 *   and the CFL_DB.dat cache), the recently-seen write with its blocklist check, and freeing the resource.
 *
 * A head takes five renderer frames (face, mask, parts, icon, copy back). The icons are kept as 64 px RGBA in a cache
 * of 17 (16 games and this console's own), keyed by the game's key and its Mii's CRC, for the whole run: coming back
 * to the list renders only new hosts. Only rows on the page shown ask, so only those render; only the Group list draws
 * them, so nothing renders during a lobby or a match. cfl-mii is loaded on the first job and let go 2 s after the
 * Group list was left (between heads). */
#include <3ds.h>
#include <citro3d.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bottom.h"
#include "cfl_mii.h"
#include "group/group_menu.h"

extern void mp_native_log(const char *);

#define ICON 64
#define CACHE 17                       /* GROUP_KEEP games and this console's own */
#define IDLE_US 2000000ull
#define RECENT_QUEUE 8
enum { EMPTY, READY, FAILED };

typedef struct {
    uint32_t key;
    uint16_t crc;
    int state;
    unsigned used;
    uint32_t px[ICON * ICON];
} Entry;

static Entry cache[CACHE];
static LightLock lock;                  /* the cache, the job, res and the I/O queue */
static int lock_ready;
static struct {
    int pending;
    uint32_t key;
    uint16_t crc;
    uint8_t mii[96];
} job;
static unsigned clock_used, serial;
static struct { int on; uint32_t key; uint16_t crc; } running;   /* the head in progress (not queued, not cached yet) */
static u64 list_seen_us;

/* cfl-mii's resource, loaded and freed by the I/O thread */
enum { RES_NONE, RES_LOADING, RES_READY, RES_FAILED, RES_FREEING };
static int res;                         /* under lock; RES_FAILED stays: no heads this run */

/* the I/O thread */
static Thread io;
static LightEvent io_wake;
static int io_event_ready;
static struct {
    int load, free_;
    int n;
    struct { uint32_t key; uint8_t mii[96]; } recent[RECENT_QUEUE];
    volatile int exited;
    u64 load_ms;
} io_q;                                  /* under lock */

/* the head in progress (the renderer's thread only) */
enum { H_IDLE, H_WAIT_RES, H_BUILD, H_ICON, H_COPY, H_DROP };
static struct {
    int state;
    uint32_t key;
    uint16_t crc;
    uint8_t mii[96];
    CFLCharModel model;
    CFLModelBuild build;
    C3D_Tex icon;
    u32 last_epoch;                      /* the frame of its last GPU pass */
    u64 t0, step_us[5];
    int steps;
} head;
static u32 epoch;                       /* the renderer's frames, counted in mp_mii_icon_between */
static int gpu_ready;                   /* CFL_InitGpu done (the renderer's thread) */

static u64 now_us(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000); }
static void init_lock(void) { if (!lock_ready) { LightLock_Init(&lock); lock_ready = 1; } }

unsigned mp_group_mii_serial(void) { return serial; }

static void mii_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void mii_log(const char *fmt, ...)
{
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    size_t n = strlen(line);
    if (n && line[n - 1] != '\n') { line[n] = '\n'; line[n + 1] = 0; }
    mp_native_log(line);
}

/* cfl-mii's printing (mii_cfl_compat.h): to the log, never the screen */
int mp_cfl_vprintf(const char *fmt, va_list ap)
{
    char line[240] = "cfl: ";
    vsnprintf(line + 5, sizeof(line) - 6, fmt, ap);
    size_t n = strlen(line);
    if (n && line[n - 1] != '\n') { line[n] = '\n'; line[n + 1] = 0; }
    mp_native_log(line);
    return 0;
}
int mp_cfl_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mp_cfl_vprintf(fmt, ap);
    va_end(ap);
    return 0;
}

const uint32_t *mp_group_mii_icon(uint32_t key, uint16_t crc, const unsigned char *mii)
{
    const uint32_t *out = NULL;
    init_lock();
    LightLock_Lock(&lock);
    for (int i = 0; i < CACHE; ++i) {
        Entry *e = &cache[i];
        if (e->state == EMPTY || e->key != key || e->crc != crc) continue;
        if (e->state == READY) { e->used = ++clock_used; out = e->px; }
        LightLock_Unlock(&lock);
        return out;                                  /* ready, or failed: the emblem */
    }
    if (res != RES_FAILED && !(job.pending && job.key == key && job.crc == crc) &&
        !(running.on && running.key == key && running.crc == crc)) {   /* already on its way */
        job.pending = 1;                             /* the latest asked wins */
        job.key = key;
        job.crc = crc;
        memcpy(job.mii, mii, sizeof(job.mii));
    }
    LightLock_Unlock(&lock);
    return NULL;
}

/* lock held */
static Entry *slot_for(uint32_t key, uint16_t crc)
{
    Entry *pick = &cache[0];
    for (int i = 0; i < CACHE; ++i) {
        if (cache[i].state != EMPTY && cache[i].key == key && cache[i].crc == crc) { pick = &cache[i]; break; }
        if (cache[i].state == EMPTY) { pick = &cache[i]; break; }
        if (cache[i].used < pick->used) pick = &cache[i];
    }
    pick->key = key;
    pick->crc = crc;
    pick->used = ++clock_used;
    return pick;
}

static void finish(int ok, const uint32_t *px)
{
    LightLock_Lock(&lock);
    Entry *e = slot_for(head.key, head.crc);
    e->state = ok ? READY : FAILED;
    if (ok) memcpy(e->px, px, sizeof(e->px));
    running.on = 0;
    ++serial;
    LightLock_Unlock(&lock);
}

/* ---- the I/O thread ---- */

static void io_main(void *arg)
{
    (void)arg;
    static uint8_t mii[96];
    for (;;) {
        LightLock_Lock(&lock);
        if (io_q.load) {
            io_q.load = 0;
            LightLock_Unlock(&lock);
            u64 t0 = now_us();
            int ok = CFL_LoadResource();
            LightLock_Lock(&lock);
            io_q.load_ms = (now_us() - t0) / 1000;
            res = ok ? RES_READY : RES_FAILED;
            LightLock_Unlock(&lock);
            continue;
        }
        if (io_q.n > 0) {
            uint32_t key = io_q.recent[0].key;
            memcpy(mii, io_q.recent[0].mii, sizeof(mii));
            memmove(&io_q.recent[0], &io_q.recent[1], sizeof(io_q.recent[0]) * (size_t)(io_q.n - 1));
            --io_q.n;
            LightLock_Unlock(&lock);
            u64 t0 = now_us();
            CFL_AddRecent((const MiiData *)mii);   /* the blocklist check, then the recently-seen write */
            mii_log("mii: recently-seen check and write for game %08lx: %llu ms", (unsigned long)key, (now_us() - t0) / 1000);
            continue;
        }
        if (io_q.free_) {
            io_q.free_ = 0;
            LightLock_Unlock(&lock);
            CFL_FreeResource();
            LightLock_Lock(&lock);
            res = RES_NONE;
            io_q.exited = 1;
            LightLock_Unlock(&lock);
            return;
        }
        LightLock_Unlock(&lock);
        LightEvent_Wait(&io_wake);
    }
}

/* The renderer's thread: a thread that has returned is joined (at once). */
static void io_reap(void)
{
    if (io && io_q.exited) {
        threadJoin(io, U64_MAX);
        threadFree(io);
        io = NULL;
        io_q.exited = 0;
    }
}

/* lock held: queue the load and start the thread */
static int io_start_load(void)
{
    if (!io_event_ready) { LightEvent_Init(&io_wake, RESET_ONESHOT); io_event_ready = 1; }
    io_q.load = 1;
    io_q.exited = 0;
    /* below the engine and the renderer: it only runs while they wait (the Group page mostly waits) */
    io = threadCreate(io_main, NULL, 16 * 1024, 0x38, -2, false);
    if (!io) { io_q.load = 0; return 0; }
    return 1;
}

/* ---- the renderer's thread ---- */

static int read_back(C3D_Tex *tex, uint32_t *out)
{
    u32 bytes = ICON * ICON * 4;
    u32 *linear = linearAlloc(bytes);
    if (!linear) return 0;
    GSPGPU_FlushDataCache(linear, bytes);
    /* outside a frame: it waits for the GPU queue, i.e. the previous frame, which the renderer's C3D_FrameBegin is
     * about to wait for anyway, then copies 16 KB */
    C3D_SyncTextureCopy(tex->data, 0, linear, 0, bytes, GX_TRANSFER_RAW_COPY(1));
    GSPGPU_InvalidateDataCache(linear, bytes);
    /* RGBA8: 8 x 8 tiles in Morton order; memory row 0 is the bottom of the image */
    for (int y = 0; y < ICON; y++) {
        int ty = ICON - 1 - y;
        for (int x = 0; x < ICON; x++) {
            int m = (x & 1) | ((ty & 1) << 1) | ((x & 2) << 1) | ((ty & 2) << 2) | ((x & 4) << 2) | ((ty & 4) << 3);
            out[y * ICON + x] = linear[((ty >> 3) * (ICON >> 3) + (x >> 3)) * 64 + m];
        }
    }
    linearFree(linear);
    return 1;
}

static void drop_head(void)
{
    CFL_DeleteModel(&head.model);
    if (head.icon.data) C3D_TexDelete(&head.icon);
    memset(&head.model, 0, sizeof(head.model));
    memset(&head.icon, 0, sizeof(head.icon));
    head.state = H_IDLE;
}

static void log_head(int ok)
{
    static const char *names[5] = {"face", "mask", "parts", "icon", "copy"};
    char steps[120];
    int len = 0;
    u64 worst = 0;
    steps[0] = 0;
    for (int i = 0; i < 5 && len < (int)sizeof(steps) - 24; ++i) {
        len += snprintf(steps + len, sizeof(steps) - (size_t)len, "%s%s %llu.%llu", i ? ", " : "", names[i],
                        head.step_us[i] / 1000, head.step_us[i] / 100 % 10);
        if (head.step_us[i] > worst) worst = head.step_us[i];
    }
    mii_log("mii: icon for game %08lx crc %04x %s: %s ms (longest step %llu ms) over %d frames, all %llu ms; free linear %lu KB, VRAM %lu KB",
         (unsigned long)head.key, head.crc, ok ? "ok" : "FAILED", steps, worst / 1000, head.steps,
         (now_us() - head.t0) / 1000, linearSpaceFree() / 1024, vramSpaceFree() / 1024);
}

/* Before the renderer's C3D_FrameBegin, outside any frame. */
void mp_mii_icon_between(void)
{
    ++epoch;
    if (!lock_ready) return;
    u64 now = now_us();
    if (group_view()->page == GROUP_PAGE_LIST) list_seen_us = now;
    /* everything drawn two frames ago is done: this frame's C3D_FrameBegin waited for the one before */
    if (CFL_PendingCount() > 0) CFL_FreePending(epoch - 2);
    io_reap();

    if (head.state == H_COPY && epoch != head.last_epoch) {
        static uint32_t px[ICON * ICON];
        u64 t = now_us();
        int ok = read_back(&head.icon, px);   /* the icon's frame is done after this (read_back waits for it) */
        head.step_us[4] = now_us() - t;
        ++head.steps;
        drop_head();                           /* the GPU is done with the model and the icon */
        finish(ok, px);
        log_head(ok);
        if (ok) {
            LightLock_Lock(&lock);             /* the recently-seen write, on the I/O thread */
            if (io && io_q.n < RECENT_QUEUE && res == RES_READY) {
                io_q.recent[io_q.n].key = head.key;
                memcpy(io_q.recent[io_q.n].mii, head.mii, sizeof(head.mii));
                ++io_q.n;
                LightEvent_Signal(&io_wake);
            }
            LightLock_Unlock(&lock);
        }
    }
    if (head.state == H_DROP && epoch - head.last_epoch >= 2) {   /* a failed head: once the GPU is done with it */
        drop_head();
        finish(0, NULL);
        log_head(0);
    }

    LightLock_Lock(&lock);
    if (head.state == H_IDLE && job.pending) {
        head.key = job.key;
        head.crc = job.crc;
        memcpy(head.mii, job.mii, sizeof(head.mii));
        job.pending = 0;
        running.on = 1;
        running.key = head.key;
        running.crc = head.crc;
        head.state = H_WAIT_RES;
        head.t0 = now;
        head.steps = 0;
        memset(head.step_us, 0, sizeof(head.step_us));
    }
    if (head.state == H_WAIT_RES && res == RES_NONE && !io) {   /* unloaded meanwhile: load again */
        if (io_start_load()) res = RES_LOADING;
        else res = RES_FAILED;
    }
    int r = res;
    u64 load_ms = io_q.load_ms;
    LightLock_Unlock(&lock);

    if (head.state == H_WAIT_RES) {
        if (r == RES_READY) {
            if (!gpu_ready) {
                u64 t = now_us();
                gpu_ready = CFL_InitGpu();
                CFL_SetAutoAddRecent(false);   /* the I/O thread does it, after the head */
                mii_log("mii: CFL_Initialize %s (resource and database %llu ms on the I/O thread, GPU set-up %llu ms)",
                     gpu_ready ? "ok" : "FAILED", load_ms, (now_us() - t) / 1000);
            }
            memset(&head.model, 0, sizeof(head.model));
            memset(&head.icon, 0, sizeof(head.icon));
            if (gpu_ready && CFL_BeginCharModel(&head.build, &head.model, (const MiiData *)head.mii, CFL_RESOLUTION_128,
                                                CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL), CFL_HAIR_NORMAL))
                head.state = H_BUILD;
            else {
                head.last_epoch = epoch;
                head.state = H_DROP;
            }
        } else if (r == RES_FAILED) {
            mii_log("mii: CFL_Initialize FAILED (no heads this run)");
            head.state = H_IDLE;
            finish(0, NULL);
        }
    }

    /* let cfl-mii go 2 s after the list was left, between heads (the cache stays) */
    if (head.state == H_IDLE && r == RES_READY && now - list_seen_us > IDLE_US) {
        LightLock_Lock(&lock);
        int idle = !job.pending;
        LightLock_Unlock(&lock);
        if (idle && CFL_PendingCount() == 0) {
            if (gpu_ready) CFL_FiniGpu();
            gpu_ready = 0;
            LightLock_Lock(&lock);
            res = RES_FREEING;
            io_q.free_ = 1;
            LightEvent_Signal(&io_wake);
            LightLock_Unlock(&lock);
            mii_log("mii: cfl-mii unloaded (the list was left); free linear %lu KB, VRAM %lu KB",
                 linearSpaceFree() / 1024, vramSpaceFree() / 1024);
        }
    }
}

/* Right after the renderer's C3D_FrameBegin: at most one GPU step, drawn into the renderer's frame. 1 if it touched the
 * GPU state (the renderer then sets its own again). */
int mp_mii_icon_step(void)
{
    if (head.state != H_BUILD && head.state != H_ICON) return 0;
    CFL_SetFrameEpoch(epoch);
    u64 t = now_us();
    if (head.state == H_BUILD) {
        int at = head.build.step == CFL_BUILD_STEP_FACE ? 0 : head.build.step == CFL_BUILD_STEP_MASK ? 1 : 2;
        CFLBuildResult r = CFL_StepCharModel(&head.build);
        head.step_us[at] += now_us() - t;
        ++head.steps;
        head.last_epoch = epoch;
        if (r == CFL_BUILD_DONE) head.state = H_ICON;
        else if (r == CFL_BUILD_FAILED) head.state = H_DROP;
        return 1;
    }
    CFLIconSetting setting = { CFL_ICON_BG_DIRECT, {0, 0, 0, 0}, NULL, NULL };
    int ok = CFL_DrawModelIcon(&head.model, CFL_EXPRESSION_NORMAL, ICON, &setting, &head.icon);
    head.step_us[3] = now_us() - t;
    ++head.steps;
    head.last_epoch = epoch;
    head.state = ok ? H_COPY : H_DROP;
    return 1;
}
