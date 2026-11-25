/* The Group list's Mii heads: the bottom screen asks for a game host's icon
 * (mp_group_mii_icon, its thread), and the renderer renders it with cfl-mii at a frame boundary on the thread that
 * owns citro3d (mp_mii_icon_frame, from renderer_begin before its C3D_FrameBegin; cfl-mii runs its own frames).
 *
 * One job at a time, the latest asked wins. A job renders a 64 px icon (transparent background), copies it to the CPU,
 * detiles it into a 4-entry cache keyed by the game's key and its Mii's CRC, and deletes the model and texture at once.
 * Only rows on the page shown ask, so only those render; only the Group list draws them, so nothing renders during a
 * lobby or a match. cfl-mii (CFL_Res.dat, about 1 MB) is loaded on the first job and let go, with the cache, 2 s
 * after the Group list was left. */
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
#define CACHE 4
#define IDLE_US 2000000ull
enum { EMPTY, READY, FAILED };

typedef struct {
    uint32_t key;
    uint16_t crc;
    int state;
    unsigned used;
    uint32_t px[ICON * ICON];
} Entry;

static Entry cache[CACHE];
static LightLock lock;
static int lock_ready;
static struct {
    int pending;
    uint32_t key;
    uint16_t crc;
    uint8_t mii[96];
} job;
static unsigned clock_used, serial;
static struct { int on; uint32_t key; uint16_t crc; } running;   /* the job being rendered (not queued, not cached yet) */
static u64 list_seen_us;
static int cfl_state;          /* 0 not loaded, 1 loaded, -1 CFL_Initialize failed (no heads this session) */

static u64 now_us(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000); }
static void init_lock(void) { if (!lock_ready) { LightLock_Init(&lock); lock_ready = 1; } }

unsigned mp_group_mii_serial(void) { return serial; }

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
    if (cfl_state >= 0 && !(job.pending && job.key == key && job.crc == crc) &&
        !(running.on && running.key == key && running.crc == crc)) {   /* already on its way */
        job.pending = 1;                             /* the latest asked wins */
        job.key = key;
        job.crc = crc;
        memcpy(job.mii, mii, sizeof(job.mii));
    }
    LightLock_Unlock(&lock);
    return NULL;
}

static Entry *slot_for(uint32_t key, uint16_t crc)
{
    Entry *pick = &cache[0];
    for (int i = 0; i < CACHE; ++i) {
        if (cache[i].state == EMPTY) { pick = &cache[i]; break; }
        if (cache[i].used < pick->used) pick = &cache[i];
    }
    pick->key = key;
    pick->crc = crc;
    pick->used = ++clock_used;
    return pick;
}

static int read_back(C3D_Tex *tex, uint32_t *out)
{
    u32 bytes = ICON * ICON * 4;
    u32 *linear = linearAlloc(bytes);
    if (!linear) return 0;
    GSPGPU_FlushDataCache(linear, bytes);
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

/* The renderer's thread, between frames. 1 if it used the GPU (the renderer then restores its own state). */
int mp_mii_icon_frame(void)
{
    char line[200];
    if (!lock_ready) return 0;
    if (group_view()->page == GROUP_PAGE_LIST) list_seen_us = now_us();
    if (cfl_state == 1 && !job.pending && now_us() - list_seen_us > IDLE_US) {
        CFL_Finalize();
        cfl_state = 0;
        LightLock_Lock(&lock);
        memset(cache, 0, sizeof(cache));
        LightLock_Unlock(&lock);
        snprintf(line, sizeof(line), "mii: cfl-mii unloaded (the list was left); free linear %lu KB, VRAM %lu KB\n",
                 linearSpaceFree() / 1024, vramSpaceFree() / 1024);
        mp_native_log(line);
        return 1;
    }
    LightLock_Lock(&lock);
    if (!job.pending) { LightLock_Unlock(&lock); return 0; }
    uint32_t key = job.key;
    uint16_t crc = job.crc;
    static uint8_t mii[96];
    memcpy(mii, job.mii, sizeof(mii));
    job.pending = 0;
    running.on = 1;
    running.key = key;
    running.crc = crc;
    LightLock_Unlock(&lock);

    u64 t0 = now_us(), t1 = t0, t2 = t0, t3 = t0;
    if (cfl_state == 0) {
        cfl_state = CFL_Initialize() ? 1 : -1;
        t1 = now_us();
        list_seen_us = t1;
        snprintf(line, sizeof(line), "mii: CFL_Initialize %s (%llu ms)\n", cfl_state > 0 ? "ok" : "FAILED",
                 (t1 - t0) / 1000);
        mp_native_log(line);
    }
    static uint32_t px[ICON * ICON];
    int ok = 0;
    if (cfl_state > 0) {
        CFLCharModel model;
        memset(&model, 0, sizeof(model));
        t1 = now_us();
        if (CFL_InitCharModel(&model, (const MiiData *)mii, CFL_RESOLUTION_128, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
            t2 = now_us();
            C3D_Tex tex;
            CFLIconSetting setting = { CFL_ICON_BG_DIRECT, {0, 0, 0, 0}, NULL, NULL };
            CFL_RebindShader();                      /* the icon pass binds no shader of its own */
            if (CFL_CommandMakeModelIcon(&model, CFL_EXPRESSION_NORMAL, ICON, &setting, &tex)) {
                ok = read_back(&tex, px);
                C3D_TexDelete(&tex);
            }
            t3 = now_us();
            CFL_DeleteModel(&model);
        }
    }
    LightLock_Lock(&lock);
    Entry *e = slot_for(key, crc);
    e->state = ok ? READY : FAILED;
    if (ok) memcpy(e->px, px, sizeof(px));
    running.on = 0;
    ++serial;
    LightLock_Unlock(&lock);
    snprintf(line, sizeof(line), "mii: icon for game %08lx crc %04x %s: model %llu ms, icon %llu ms, all %llu ms; free linear %lu KB, VRAM %lu KB\n",
             (unsigned long)key, crc, ok ? "ok" : "FAILED", (t2 - t1) / 1000, (t3 - t2) / 1000, (now_us() - t0) / 1000,
             linearSpaceFree() / 1024, vramSpaceFree() / 1024);
    mp_native_log(line);
    return 1;
}
