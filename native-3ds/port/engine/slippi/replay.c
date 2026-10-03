/* Slippi replay playback, the determinism oracle.
 *
 * The native form of Slippi's playback codes (Playback/Core), after Melee
 * Unlocked's shim/mu_replay.c (GPL-3.0-or-later): boot into the debug VS
 * mode, start the match from the replay's game info block and seed, force each
 * frame's start seed, and write the recorded inputs into the fighters where
 * RestoreGameFrame (8006b0dc) does. Every fighter's pre-frame and post-frame
 * state is written to sdmc:/3ds/melee/slippi/replay-out.bin for
 * tools/slippi/replay_compare.py to check against the recording.
 *
 * Input: sdmc:/3ds/melee/slippi/replay.bin, made by tools/slippi/replay_pack.py.
 * Absent file: everything here is inert.
 */
#include <string.h>
#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gmvs.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <melee/mp/mpcoll.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/random.h>
#include <slippi_engine.h>

_Static_assert(sizeof(StartMeleeData) == 0x138, "game info block layout");

void mp_platform_log(const char* text);
#include <stdio.h>

#define REPLAY_PATH "sdmc:/3ds/melee/slippi/replay.bin"
#define OUT_PATH "sdmc:/3ds/melee/slippi/replay-out.bin"
#define ONLINE_OUT_PATH "sdmc:/3ds/melee/slippi/online-out.bin"
#define FIRST_FRAME (-123)
#define ENTRY_SIZE (raw_mode ? 40 : 32)
#define HEADER_SIZE 0x14C

static int loaded;            /* 0 = not tried, 1 = active, -1 = absent */
static int raw_mode;          /* SLR2: raw pads through the game's pad code */
static int dump_first = 1, dump_last = 0;   /* dump.txt: whole Fighter structs */
static const u8* blob;
static unsigned blob_size;
static const u8** frame_ptr;  /* per replay frame, its record */
static int first_frame;
static unsigned frame_count;
static int frame_index = FIRST_FRAME;
static int terminated;
static int finished;

/* Replay runs write in 64 KiB pieces. (Long runs that once stalled on Azahar
 * were paused by the smoke build's scripted Start press, since disabled.)
 * An online recording (config record=1) stays in a RAM buffer until the
 * match ends: on the console every SD write reopens the file, which costs
 * about a quarter of a second. */
static u8 replay_buf[64 * 1024];
static u8* out_buf = replay_buf;
static unsigned out_cap = sizeof replay_buf;
static unsigned out_len;
static int out_full;
static int out_started;
static int online_rec;        /* recording an online match (no injection) */

static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
static float bef(const u8* p) { u32 v = be32(p); float f; memcpy(&f, &v, 4); return f; }

static void logf_(const char* fmt, int a, int b)
{
    char line[160];
    snprintf(line, sizeof line, fmt, a, b);
    mp_platform_log(line);
}

static int load(void)
{
    unsigned i, off;
    if (loaded) {
        return loaded > 0;
    }
    loaded = -1;
    blob = mp_platform_slippi_file_load(REPLAY_PATH);
    if (blob == NULL) {
        return 0;
    }
    blob_size = mp_platform_slippi_file_size();
    raw_mode = blob_size >= 4 && memcmp(blob, "SLR2", 4) == 0;
    if (blob_size < HEADER_SIZE || (memcmp(blob, "SLR1", 4) != 0 && !raw_mode)) {
        mp_platform_log("Slippi replay: bad replay.bin\n");
        return 0;
    }
    first_frame = (int) be32(blob + 0x144);
    frame_count = be32(blob + 0x148);
    frame_ptr = mp_platform_alloc(frame_count * sizeof(*frame_ptr));
    off = HEADER_SIZE;
    for (i = 0; i < frame_count; i++) {
        if (off + 8 > blob_size) {
            mp_platform_log("Slippi replay: truncated replay.bin\n");
            frame_count = i;
            break;
        }
        frame_ptr[i] = blob + off;
        off += 8 + blob[off + 4] * ENTRY_SIZE;
    }
    mp_platform_slippi_unlimited();
    mp_platform_slippi_load_log(1);   /* which files a match load opens */
    {
        /* sdmc:/3ds/melee/slippi/dump.txt "FIRST LAST": dump every fighter's
         * whole struct at its post-frame point for those replay frames. */
        const char* d = mp_platform_slippi_file_load("sdmc:/3ds/melee/slippi/dump.txt");
        if (d != NULL) {
            int v[2] = {0, 0}, k = 0, sign = 1;
            unsigned n = mp_platform_slippi_file_size(), j;
            for (j = 0; j <= n && k < 2; j++) {
                char c = j < n ? d[j] : ' ';
                if (c == '-') {
                    sign = -1;
                } else if (c >= '0' && c <= '9') {
                    v[k] = v[k] * 10 + (c - '0');
                } else if (j > 0 && d[j - 1] >= '0' && d[j - 1] <= '9') {
                    v[k] *= sign;
                    sign = 1;
                    k++;
                }
            }
            dump_first = v[0];
            dump_last = v[1];
            mp_platform_free((void*) d);
            mp_platform_slippi_file_write("sdmc:/3ds/melee/slippi/fighter-dump.bin", "", 0, 0);
        }
    }
    loaded = 1;
    logf_("Slippi replay: %d frames from %d\n", (int) frame_count, first_frame);
    return 1;
}

int mp_slippi_replay_on(void)
{
    return load();
}

int mp_slippi_replay_frame_index(void)
{
    return frame_index;
}

static const u8* frame_record(int frame)
{
    int i = frame - first_frame;
    if (!mp_slippi_replay_on() || i < 0 || (unsigned) i >= frame_count) {
        return NULL;
    }
    return frame_ptr[i];
}

static const u8* entry_for(int frame, int port, int follower)
{
    const u8* rec = frame_record(frame);
    int i, n;
    if (rec == NULL) {
        return NULL;
    }
    n = rec[4];
    for (i = 0; i < n; i++) {
        const u8* e = rec + 8 + i * ENTRY_SIZE;
        if (e[0] == port && e[1] == follower) {
            return e;
        }
    }
    return NULL;
}

/* ---- output ---- */

static void out_flush(void)
{
    if (out_len == 0) {
        return;
    }
    mp_platform_slippi_file_write(online_rec ? ONLINE_OUT_PATH : OUT_PATH, out_buf, out_len, out_started);
    out_started = 1;
    out_len = 0;
}

static void put32(u8* b, u32 v)
{
    b[0] = (u8) (v >> 24);
    b[1] = (u8) (v >> 16);
    b[2] = (u8) (v >> 8);
    b[3] = (u8) v;
}

static void putf(u8* b, float f)
{
    u32 v;
    memcpy(&v, &f, 4);
    put32(b, v);
}

/* 40-byte record: type, port, follower, stocks, frame, seed, action(u16), pad,
 * x, y, facing, percent, shield, animation frame. */
static void out_record(u8 type, Fighter* fp)
{
    u8* b;
    if (out_len + 40 > out_cap) {
        if (online_rec) {
            out_full = 1;
            return;
        }
        out_flush();
    }
    b = out_buf + out_len;
    b[0] = type;
    b[1] = fp->player_id;
    b[2] = (u8) (fp->x221F_b4 && fp->kind == Ft_Kind_Nana);
    b[3] = (u8) Player_GetStocks(fp->player_id);
    put32(b + 4, (u32) frame_index);
    put32(b + 8, *seed_ptr);
    b[12] = (u8) (fp->motion_id >> 8);
    b[13] = (u8) fp->motion_id;
    b[14] = b[15] = 0;
    putf(b + 16, fp->cur_pos.x);
    putf(b + 20, fp->cur_pos.y);
    putf(b + 24, fp->facing_dir);
    putf(b + 28, fp->dmg.x1830_percent);
    putf(b + 32, fp->shield_health);
    putf(b + 36, fp->cur_anim_frame);
    out_len += 40;
}

static void out_end(void)
{
    u8* b;
    if (out_len + 40 > out_cap) {
        if (online_rec) {
            out_full = 1;
            out_len = out_cap - 40;   /* the end marker replaces the last record */
        } else {
            out_flush();
        }
    }
    b = out_buf + out_len;
    memset(b, 0, 40);
    b[0] = 'E';
    put32(b + 4, (u32) frame_index);
    out_len += 40;
    out_flush();
}

/* ---- raw mode: the recorded pads enter where PADRead's results do ----
 * Called at the top of each engine body, before HSD_PadRenewMasterStatus
 * reads the queue entry: the entry gets each port's raw stick, c-stick,
 * physical buttons and triggers (recovered from the processed L/R floats). */
static u8 raw_trigger(const u8* f)
{
    PadLibData* p = &HSD_PadLibData;
    float v = bef(f);
    int raw;
    if (v <= 0.0f) {
        return 0;
    }
    raw = (int) (v * (float) p->scale_analogLR + 0.5f);
    if (p->clamp_analogLRShift == 1) {
        raw += p->clamp_analogLRMin;
    }
    return (u8) (raw > 255 ? 255 : raw);
}

void mp_slippi_replay_body_begin(void)
{
    PadLibData* p = &HSD_PadLibData;
    const u8* rec;
    int frame, i, n;
    if (!mp_slippi_replay_on() || !raw_mode || terminated || p->qcount == 0) {
        return;
    }
    frame = gm_801A4BA8() == 0 ? FIRST_FRAME : frame_index + 1;
    rec = frame_record(frame);
    if (rec == NULL) {
        return;
    }
    n = rec[4];
    for (i = 0; i < n; i++) {
        const u8* e = rec + 8 + i * ENTRY_SIZE;
        PADStatus* pad;
        if (e[1] != 0 || e[0] > 3) {
            continue;
        }
        pad = &p->queue[p->qread].stat[e[0]];
        memset(pad, 0, sizeof *pad);
        pad->stickX = (s8) e[2];
        pad->stickY = (s8) e[3];
        pad->substickX = (s8) e[4];
        pad->substickY = (s8) e[5];
        pad->button = (u16) (e[6] << 8 | e[7]);
        pad->triggerLeft = raw_trigger(e + 32);
        pad->triggerRight = raw_trigger(e + 36);
    }
}

/* 'Q' record: the processed inputs a fighter got (raw mode). */
static void out_inputs(Fighter* fp)
{
    u8* b;
    if (out_len + 40 > out_cap) {
        if (online_rec) {
            out_full = 1;
            return;
        }
        out_flush();
    }
    b = out_buf + out_len;
    memset(b, 0, 40);
    b[0] = 'Q';
    b[1] = fp->player_id;
    put32(b + 4, (u32) frame_index);
    putf(b + 16, fp->input.lstick[0].x);
    putf(b + 20, fp->input.lstick[0].y);
    putf(b + 24, fp->input.cstick[0].x);
    putf(b + 28, fp->input.cstick[0].y);
    putf(b + 32, fp->input.triggers[0]);
    put32(b + 36, fp->input.held_buttons[0]);
    out_len += 40;
}

/* ---- the 3DS's own record of an online match ----
 * Same records as playback, with frame = unk_8 - 123 (the replay frame index of
 * the PC's .slp), plus 'I' records: the two wire pads each engine body used.
 * tools/slippi/online_compare.py lines it up with the PC's replay. */
#define ONLINE_RECORD_BYTES (4u << 20)   /* ~5 records/frame: about 6 minutes */

void mp_slippi_record_online_begin(void)
{
    static u8* online_buf;
    if (online_buf == NULL) {
        online_buf = mp_platform_alloc(ONLINE_RECORD_BYTES);
    }
    if (online_buf == NULL) {
        mp_platform_log("Slippi online: no memory for the match record\n");
        return;
    }
    out_buf = online_buf;
    out_cap = ONLINE_RECORD_BYTES;
    online_rec = 1;
    out_len = 0;
    out_full = 0;
    out_started = 0;
    frame_index = FIRST_FRAME;
}

void mp_slippi_record_online_frame(int engine_frame)
{
    if (!online_rec) {
        return;
    }
    frame_index = engine_frame + FIRST_FRAME;
}

void mp_slippi_record_online_inputs(const unsigned char* port0, const unsigned char* port1, unsigned checksum)
{
    u8* b;
    if (!online_rec) {
        return;
    }
    if (out_len + 40 > out_cap) {
        if (online_rec) {
            out_full = 1;
            return;
        }
        out_flush();
    }
    b = out_buf + out_len;
    memset(b, 0, 40);
    b[0] = 'I';
    put32(b + 4, (u32) frame_index);
    put32(b + 8, checksum);
    memcpy(b + 16, port0, 8);
    memcpy(b + 24, port1, 8);
    out_len += 40;
}

void mp_slippi_record_online_end(void)
{
    if (!online_rec) {
        return;
    }
    out_end();
    online_rec = 0;
    out_buf = replay_buf;
    out_cap = sizeof replay_buf;
    if (out_full) {
        mp_platform_log("Slippi online: the match record filled up; it holds the first part\n");
    }
}

/* ---- boot (Boot to Playback Scene, 801a45a0) ---- */

void mp_slippi_replay_boot_mode(u8* mode)
{
    if (mp_slippi_replay_on()) {
        *mode = GM_DEBUG_VS;
    }
}

/* The waiting scene's preloads (SceneThink_Playback, 801a6348), just before the
 * match state's own. */
void mp_slippi_replay_prepare_scene(void)
{
    const u8* info;
    PreloadedGameModeState* scene;
    u64 mask = 0;
    int i;

    if (mp_slippi_replay_on()) {
        info = blob + 8;
    } else if (mp_slippi_online_wait_match()) {
        info = mp_slippi_online_pending_block();
    } else {
        return;
    }
    scene = lbDvd_GetPreloadCacheScene();
    for (i = 0; i < 4; i++) {
        const u8* p = info + 0x60 + 0x24 * i;
        scene->game_cache.entries[i].char_id = (s8) p[0];
        scene->game_cache.entries[i].color = p[3];
    }
    lbDvd_80018254();
    lbDvd_80018C2C(199);
    lbDvd_80017700(4);
    lbAudioAx_80026F2C(28);
    for (i = 0; i < 6; i++) {
        const u8* p = info + 0x60 + 0x24 * i;
        if ((s8) p[0] != 33) {
            mask |= lbAudioAx_80026E84((CharacterKind) (s8) p[0]);
        }
    }
    mask |= lbAudioAx_80026EBC((StKind) ((info[0xE] << 8) | info[0xF]));
    lbAudioAx_8002702C(4, mask);
    lbAudioAx_80027168();
    lbAudioAx_80024F6C();
}

/* ---- RestoreGameInfo (8016e748) ---- */

static void restore_rng_proc(HSD_GObj* gobj)
{
    const u8* rec = frame_record(frame_index);
    (void) gobj;
    if (rec != NULL && !terminated) {
        *seed_ptr = be32(rec);
    }
}

void mp_slippi_replay_start_melee(StartMeleeData* data)
{
    HSD_GObj* gobj;
    if (!mp_slippi_replay_on()) {
        return;
    }
    /* The block is the console layout, which the big-endian engine shares.
     * Its pause and match callbacks (0x38..0x5B) are console addresses. */
    memcpy(data, blob + 8, sizeof(StartMeleeData));
    memset((u8*) data + 0x38, 0, 0x5C - 0x38);
    *seed_ptr = be32(blob + 0x140);
    frame_index = FIRST_FRAME;
    terminated = 0;
    finished = 0;
    out_len = 0;
    out_started = 0;
    /* RestoreInitialRNG: GObj class 4, p_link 7, priority 0; process s_link 0. */
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, restore_rng_proc, 0);
    logf_("Slippi replay: match starts, stage %d, seed %08X\n",
          (blob[8 + 0xE] << 8) | blob[8 + 0xF], (int) *seed_ptr);
}

/* ---- IncrementFrameIndex / FetchGameFrame (8016d294), fn_8016CFE0 ---- */

void mp_slippi_replay_scene_think(int match_result)
{
    if (!mp_slippi_replay_on() || finished) {
        return;
    }
    if (match_result != 0) {
        /* GAME! (or a no contest): the results screen may wait for a button. */
        mp_slippi_replay_match_exit();
        return;
    }
    if (gm_801A4BA8() == 0) {
        frame_index = FIRST_FRAME;
    } else {
        frame_index++;
    }
    if (frame_index % 600 == 0) {
        out_flush();   /* partial results survive a crash */
    }
    if (match_result == 0 && frame_record(frame_index) == NULL && !terminated) {
        terminated = 1;
        logf_("Slippi replay: recording ends after frame %d (%d)\n", frame_index - 1, 0);
    }
}

int mp_slippi_replay_terminated(void)
{
    return mp_slippi_replay_on() && terminated && !finished;
}

/* ---- RestoreGameFrame (8006b0dc) ---- */

void mp_slippi_replay_input(Fighter* fp)
{
    if (online_rec) {
        if (!fp->x221F_b3) {
            out_record('P', fp);
        }
        return;
    }
    const u8* e;
    int follower, qread;
    PADStatus* pad;

    if (!mp_slippi_replay_on() || terminated) {
        return;
    }
    follower = fp->x221F_b4 && fp->kind == Ft_Kind_Nana;
    if (raw_mode) {
        if (!follower) {
            out_inputs(fp);
        }
        goto spawn;
    }
    e = entry_for(frame_index, fp->player_id, follower);
    if (e == NULL) {
        out_record('p', fp);   /* no recorded input for this fighter */
        return;
    }
    if (!follower) {
        fp->input.lstick[0].x = bef(e + 8);
        fp->input.lstick[0].y = bef(e + 12);
        fp->input.cstick[0].x = bef(e + 16);
        fp->input.cstick[0].y = bef(e + 20);
        fp->input.triggers[0] = bef(e + 24);
        fp->input.held_buttons[0] = be32(e + 28);
        /* UCF reads the raw stick of the pad queue entry this frame's input
         * came from. */
        qread = HSD_PadLibData.qread - 1;
        if (qread < 0) {
            qread += HSD_PadLibData.qnum;
        }
        pad = &HSD_PadLibData.queue[qread].stat[fp->x618_player_id];
        pad->stickX = (s8) e[2];
        pad->stickY = (s8) e[3];
        pad->substickX = (s8) e[4];
        pad->substickY = (s8) e[5];
        {
            extern PADStatus mp_pad_consumed[4];
            PADStatus* c = &mp_pad_consumed[fp->x618_player_id & 3];
            c->stickX = (s8) e[2];
            c->stickY = (s8) e[3];
            c->substickX = (s8) e[4];
            c->substickY = (s8) e[5];
        }
    }
spawn:
    /* Spawn correction on the first frame. */
    if (frame_index == FIRST_FRAME) {
        CmSubject* box;
        ftPartSetRotX(fp, 0, 0.0f);
        fp->coll_data.cur_pos = fp->cur_pos;
        fp->coll_data.last_pos = fp->cur_pos;
        fp->mv.co.entry.x4 = fp->cur_pos.y;
        fp->coll_data.x38 = mpColl_804D64AC;
        Player_80032828(fp->player_id, fp->x221F_b4, &fp->cur_pos);
        ftCamera_UpdateCameraBox(fp->gobj);
        box = fp->x890_cameraBox;
        box->ext.h.x = box->target_ext.h.x;
        box->ext.h.y = box->target_ext.h.y;
        Camera_8002F3AC();
    }
    out_record('P', fp);
}

static void dump_fighter(Fighter* fp)
{
    u8 head[16];
    if (frame_index < dump_first || frame_index > dump_last) {
        return;
    }
    memset(head, 0, sizeof head);
    put32(head, (u32) frame_index);
    head[4] = fp->player_id;
    head[5] = (u8) (fp->x221F_b4 && fp->kind == Ft_Kind_Nana);
    put32(head + 8, (u32) sizeof(Fighter));
    mp_platform_slippi_file_write("sdmc:/3ds/melee/slippi/fighter-dump.bin", head, sizeof head, 1);
    mp_platform_slippi_file_write("sdmc:/3ds/melee/slippi/fighter-dump.bin", fp, sizeof(Fighter), 1);
    {
        extern const void* mp_ucf_history(unsigned* size);
        unsigned n;
        const void* h = mp_ucf_history(&n);
        head[4] = (u8) (0x80 | fp->player_id);
        put32(head + 8, n);
        mp_platform_slippi_file_write("sdmc:/3ds/melee/slippi/fighter-dump.bin", head, sizeof head, 1);
        mp_platform_slippi_file_write("sdmc:/3ds/melee/slippi/fighter-dump.bin", h, n, 1);
    }
}

/* SendGamePostFrame's point: after the camera callback. */
void mp_slippi_replay_post_frame(Fighter* fp)
{
    if (!online_rec && mp_slippi_replay_on() && !terminated && !fp->x221F_b3) {
        dump_fighter(fp);
    }
    if (online_rec) {
        if (!fp->x221F_b3) {
            out_record('O', fp);
        }
        return;
    }
    if (!mp_slippi_replay_on() || terminated || fp->x221F_b3) {
        return;
    }
    out_record('O', fp);
}

void mp_slippi_replay_match_exit(void)
{
    if (!mp_slippi_replay_on() || finished) {
        return;
    }
    finished = 1;
    out_end();
    mp_platform_log("SLIPPI REPLAY DONE\n");
}
