/* Slippi online play, engine side: a lockstep peer.
 *
 * Slippi's online codes (Online/Core: TriggerSendInput, StartEngineLoop,
 * InitOnlinePlay) as native code, after Melee Unlocked's shim/mu_online.c
 * (GPL-3.0-or-later), minus prediction and rollback. A frame only runs once
 * the remote pad for it has arrived; until then the pad sample is skipped,
 * exactly as Slippi's games skip samples when one runs ahead. The PC peer
 * predicts and rolls back on its own, so it never notices the difference.
 *
 * Frame numbers: online pad frame N is consumed by the engine body whose
 * unpaused-frame counter (gm_80479D58.unk_8) is N at its start.
 *
 * Active when sdmc:/3ds/melee/slippi/config.ini names an opponent:
 *   opponent=ABCD#123   character=2  color=0  stage=32  stage_select=1  delay=4
 */
#include <stdio.h>
#include <string.h>
#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>
#include <slippi_rules.h>

void mp_platform_log(const char* text);
void mp_slippi_gmvs_end_online(int pauser);   /* gmvs.c (tools/slippi_edits/online.py) */
int mp_slippi_engine_leaving(void);          /* gm_1A45.c (tools/slippi_edits/online.py) */
extern StaticPlayer player_slots[];

#define CONFIG_PATH "sdmc:/3ds/melee/slippi/config.ini"

enum {
    PAD_SIZE = 12,
    MIN_DELAY = 1,
    MAX_DELAY = 15,
    UNFREEZE_FRAME = 84,
    START_SYNC_FRAME = UNFREEZE_FRAME - 6,
    DESYNC_ENTRIES = 21,
    RESP_NORMAL = 1,
    RESP_SKIP = 2,
    RESP_DISCONNECTED = 3,
    BLOCK_SIZE = 0x138,
};

typedef struct {
    int frame;
    u32 checksum;
} DesyncLocal;

static struct {
    int configured;           /* 0 unknown, 1 online, -1 offline */
    char opponent[24];
    int character, color, stage, stage_select, delay_setting, record;
    int test_inputs, test_end_frame;   /* automated tests (config.ini) */
    int prefetched_remote;
    int started;              /* matchmaking started */
    int selections_sent;
    int pending;              /* match negotiated, waiting for its scene */
    u8 block[BLOCK_SIZE + 8];
    int active;
    u8 local_index, remote_index, delay;
    u32 rng_offset;
    int frame;
    u8 last_local[PAD_SIZE];
    u8 delay_index;
    u8 delay_buffer[MAX_DELAY][PAD_SIZE];
    int finalized, stable_finalized;
    u32 tx_checksum;
    int desync_last_frame;
    u8 desync_write_idx;
    DesyncLocal desync_local[DESYNC_ENTRIES];
    int disconnected, disconnect_shown, desync_shown, game_over, game_end_frame;
    unsigned skips, skip_run, frames, waits, wait_ms, longest_wait_ms, no_sample_bodies;
    unsigned load_mark_ms, load_mark_opens, load_mark_open_ms, load_mark_bytes, load_logging;
    int advance_left, advance_gap, advancing, drop_samples, last_drop_frame;
    unsigned advances, dropped;
} on;

static void logf_(const char* fmt, int a, int b, int c)
{
    char line[200];
    snprintf(line, sizeof line, fmt, a, b, c);
    mp_platform_log(line);
}

static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }

static int global_frame(void)
{
    return (int) gm_801A4BB8();
}

/* ---- configuration ---- */

static int parse_int(const char* s, int fallback)
{
    int v = 0, sign = 1, any = 0;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
        any = 1;
    }
    return any ? v * sign : fallback;
}

static void load_config(void)
{
    const char* text;
    unsigned size, i = 0;
    if (on.configured) {
        return;
    }
    on.configured = -1;
    on.prefetched_remote = -1;
    on.character = 2;   /* Fox */
    on.color = 0;
    on.stage = 32;      /* Final Destination */
    on.stage_select = 1;
    on.delay_setting = 4;
    text = mp_platform_slippi_file_load(CONFIG_PATH);
    if (text == NULL) {
        return;
    }
    size = mp_platform_slippi_file_size();
    while (i < size) {
        char line[96];
        unsigned n = 0;
        while (i < size && text[i] != '\n' && n + 1 < sizeof line) {
            if (text[i] != '\r') {
                line[n++] = text[i];
            }
            i++;
        }
        while (i < size && text[i] != '\n') {
            i++;
        }
        i++;
        line[n] = 0;
        if (line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (strncmp(line, "opponent=", 9) == 0) {
            strncpy(on.opponent, line + 9, sizeof on.opponent - 1);
        } else if (strncmp(line, "character=", 10) == 0) {
            on.character = parse_int(line + 10, on.character);
        } else if (strncmp(line, "color=", 6) == 0) {
            on.color = parse_int(line + 6, on.color);
        } else if (strncmp(line, "stage=", 6) == 0) {
            on.stage = parse_int(line + 6, on.stage);
        } else if (strncmp(line, "stage_select=", 13) == 0) {
            on.stage_select = parse_int(line + 13, on.stage_select);
        } else if (strncmp(line, "test_inputs=", 12) == 0) {
            on.test_inputs = parse_int(line + 12, 0);
        } else if (strncmp(line, "test_end_frame=", 15) == 0) {
            on.test_end_frame = parse_int(line + 15, 0);
        } else if (strncmp(line, "record=", 7) == 0) {
            on.record = parse_int(line + 7, 0);
        } else if (strncmp(line, "delay=", 6) == 0) {
            on.delay_setting = parse_int(line + 6, on.delay_setting);
        }
    }
    mp_platform_free((void*) text);
    if (mp_platform_slippi_session() == 2) {
        return;   /* the boot menu chose offline */
    }
    if (on.opponent[0] != 0) {
        on.configured = 1;
        mp_platform_slippi_unlimited();
        logf_("Slippi online: configured (character %d, stage %d, delay %d)\n", on.character, on.stage,
              on.delay_setting);
    }
}

int mp_slippi_online_configured(void)
{
    load_config();
    return on.configured > 0;
}

/* ---- pads ---- */

static void pad_to_wire(u8* out, const PADStatus* pad)
{
    out[0] = (u8) (pad->button >> 8);
    out[1] = (u8) pad->button;
    out[2] = (u8) pad->stickX;
    out[3] = (u8) pad->stickY;
    out[4] = (u8) pad->substickX;
    out[5] = (u8) pad->substickY;
    out[6] = pad->triggerLeft;
    out[7] = pad->triggerRight;
    out[8] = pad->analogA;
    out[9] = pad->analogB;
    out[10] = (u8) pad->err;
    out[11] = 0;
}

static void wire_to_pad(PADStatus* pad, const u8* in)
{
    pad->button = (u16) (in[0] << 8 | in[1]);
    pad->stickX = (s8) in[2];
    pad->stickY = (s8) in[3];
    pad->substickX = (s8) in[4];
    pad->substickY = (s8) in[5];
    pad->triggerLeft = in[6];
    pad->triggerRight = in[7];
    pad->analogA = in[8];
    pad->analogB = in[9];
    pad->err = (s8) in[10];
}

static void clamp_stick_at_rest(PADStatus* pad)
{
    if (pad->stickX >= -2 && pad->stickX <= 2 && pad->stickY >= -2 && pad->stickY <= 2) {
        pad->stickX = pad->stickY = 0;
    }
    if (pad->substickX >= -2 && pad->substickX <= 2 && pad->substickY >= -2 && pad->substickY <= 2) {
        pad->substickX = pad->substickY = 0;
    }
}

/* ---- the desync checksum (StartEngineLoop FN_COMPUTE_CHECKSUM) ---- */

static u32 float_bits(float f)
{
    u32 v;
    memcpy(&v, &f, 4);
    return v;
}

static u32 compute_checksum(void)
{
    u32 acc = 0;
    float sum = 0.0F;
    int i, k;
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 2; k++) {
            HSD_GObj* gobj = player_slots[i].player_entity[k];
            Fighter* fp;
            if (gobj == NULL) {
                continue;
            }
            fp = gobj->user_data;
            acc ^= (u32) fp->motion_id;
            acc ^= float_bits(fp->cur_pos.x);
            acc ^= float_bits(fp->cur_pos.y);
            acc ^= float_bits(fp->dmg.x1830_percent);
            acc ^= (u32) fp->x8_spawnNum;
            sum = sum + fp->cur_pos.x;
            sum = sum + fp->cur_pos.y;
            sum = sum + fp->dmg.x1830_percent;
        }
        acc ^= (u8) player_slots[i].stocks;
    }
    return (((acc >> 16) ^ (acc & 0xFFFF)) << 16) | ((u32) (s32) sum & 0xFFFF);
}

/* Match-load timing: each mark logs the time, SD opens and bytes since the
 * previous one (the "match ready" mark starts the series). */
static void load_mark(const char* what, int start)
{
    unsigned ms = mp_platform_slippi_ms(), opens = mp_platform_slippi_sd_stat(0),
             open_ms = mp_platform_slippi_sd_stat(1), bytes = mp_platform_slippi_sd_stat(2);
    char line[200];
    if (!start) {
        snprintf(line, sizeof line,
                 "Slippi load: %s after %u ms (SD opens %u taking %u ms, %u KB read; %u reads from RAM so far)\n",
                 what, ms - on.load_mark_ms, opens - on.load_mark_opens, open_ms - on.load_mark_open_ms,
                 (bytes - on.load_mark_bytes) / 1024, mp_platform_slippi_prefetch_hits());
        mp_platform_log(line);
    }
    on.load_mark_ms = ms;
    on.load_mark_opens = opens;
    on.load_mark_open_ms = open_ms;
    on.load_mark_bytes = bytes;
}

static int hooks_on(void)
{
    return on.active && mp_slippi_engine_leaving() == 0;
}

/* ---- the pads of each engine body ----
 * Slippi assigns online frame N to the pad sample that the engine body whose
 * unpaused-frame counter (gm_80479D58.unk_8) is N consumes (Melee Unlocked
 * measured this on the console code). The 3DS loads matches more slowly, so
 * its pad alarm keeps firing during the match's on_enter; numbering samples at
 * PADRead time would hand online frames to samples the scene loop then
 * flushes, shifting every remote input. So the exchange happens here, at the
 * top of the body, on the queue entry the body is about to read, and the body
 * waits until the remote pad for its frame has arrived. */

/* Right after PADRead: nonzero drops the sample, so the engine runs one
 * frame fewer this period. Used by the time sync (mp_slippi_online_frame_end)
 * when this 3DS runs ahead of the opponent: its pad timer is exactly 60 Hz,
 * Dolphin's frames are 59.94 Hz, and a peer that drifts ahead makes the PC
 * believe it is behind (rollbacks, speed-ups and Dolphin's "poor match
 * performance" banner). Dolphin drops samples the same way (skip frames). */
int mp_slippi_online_pad_renew(PADStatus* stat)
{
    (void) stat;
    if (!hooks_on() || on.drop_samples <= 0) {
        return 0;
    }
    on.drop_samples--;
    on.dropped++;
    return 1;
}

void mp_platform_idle(void);

/* Automated tests (test_inputs=SEED): a deterministic pad per frame in place
 * of the 3DS controls, held a few frames like a person would, never Start;
 * test_end_frame=N pauses at N and ends the match with L+R+A+Start. */
static void test_pad(PADStatus* pad, int frame)
{
    u32 x = (u32) (frame / 6) * 2654435761u ^ (u32) on.test_inputs * 40503u;
    static const u16 buttons[] = {0, 0, 0, 0x0100, 0x0200, 0x0400, 0x0040, 0x0100, 0x0020, 0};
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    memset(pad, 0, sizeof *pad);
    pad->stickX = (s8) ((int) ((x >> 3) % 161) - 80);
    pad->stickY = (s8) ((int) ((x >> 11) % 161) - 80);
    if ((x >> 20) % 5 == 0) {
        pad->substickX = (s8) ((int) ((x >> 23) % 161) - 80);
    }
    pad->button = buttons[(x >> 27) % 10];
    if (pad->button & 0x0040) {
        pad->triggerLeft = 140;
    }
    if (on.test_end_frame > 0 && frame >= on.test_end_frame) {
        memset(pad, 0, sizeof *pad);
        if (frame < on.test_end_frame + 3) {
            pad->button = 0x1000;                        /* Start: pause */
        } else if (frame > on.test_end_frame + 20) {
            pad->button = 0x1000 | 0x0040 | 0x0020 | 0x0100;   /* L+R+A+Start */
            pad->triggerLeft = pad->triggerRight = 200;
        }
    }
}

static void exchange_pads(int frame)
{
    PadLibData* p = &HSD_PadLibData;
    PADStatus* stat;
    PADStatus* source;
    u8 remote[PAD_SIZE];
    int i, result, waited = 0;

    if (p->qcount == 0 || p->queue == NULL) {
        if (on.no_sample_bodies++ < 8) {
            logf_("Slippi online: body %d has no pad sample\n", frame, 0, 0);
        }
        return;
    }
    stat = p->queue[p->qread].stat;
    if (frame < 1) {
        memset(stat, 0, 4 * sizeof *stat);
        return;
    }
    if (frame < START_SYNC_FRAME - on.delay) {
        memset(stat, 0, 4 * sizeof *stat);
    }
    source = &stat[0];   /* the 3DS controls are port 1's pad */
    if (on.test_inputs && frame >= START_SYNC_FRAME - on.delay) {
        test_pad(source, frame);
    }
    clamp_stick_at_rest(source);
    if (source->err == -3) {
        wire_to_pad(source, on.last_local);
    }
    pad_to_wire(on.last_local, source);
    /* The opponent only ever sees the first 8 bytes; simulate ours the same way. */
    memset(on.last_local + 8, 0, PAD_SIZE - 8);

    for (i = 0; i < DESYNC_ENTRIES; i++) {
        if (on.desync_local[i].frame == on.stable_finalized) {
            on.tx_checksum = on.desync_local[i].checksum;
            break;
        }
    }
    for (;;) {
        mp_platform_slippi_net_poll();
        result = mp_platform_slippi_net_send_inputs(frame, on.delay, on.stable_finalized, on.tx_checksum,
                                                    on.last_local, remote);
        if (result != RESP_SKIP || on.game_over) {
            break;
        }
        if (waited++ == 0) {
            on.waits++;
        }
        if ((waited & 15) == 0) {
            mp_platform_slippi_wait_poll();
        }
        mp_platform_idle();   /* about 1 ms */
    }
    on.wait_ms += (unsigned) waited;
    if ((unsigned) waited > on.longest_wait_ms) {
        on.longest_wait_ms = (unsigned) waited;
    }
    if (result != RESP_NORMAL) {
        if (!on.disconnected) {
            logf_("Slippi online: disconnected at frame %d (%d)\n", frame, result, 0);
        }
        on.disconnected = 1;
        memset(remote, 0, sizeof remote);
    }
    /* The local player's pad is the one from `delay` frames ago. */
    wire_to_pad(&stat[on.local_index], on.delay_buffer[on.delay_index]);
    memcpy(on.delay_buffer[on.delay_index], on.last_local, PAD_SIZE);
    on.delay_index = (u8) ((on.delay_index + 1) % on.delay);
    wire_to_pad(&stat[on.remote_index], remote);
    {
        u8 w0[PAD_SIZE], w1[PAD_SIZE];
        pad_to_wire(w0, &stat[0]);
        pad_to_wire(w1, &stat[1]);
        mp_slippi_record_online_inputs(w0, w1, on.tx_checksum);
    }
    on.finalized = frame;
    on.frames++;
    if (frame % 600 == 0) {
        logf_("Slippi online: frame %d, waited for the opponent %d times (%d ms)\n", frame, (int) on.waits,
              (int) on.wait_ms);
        logf_("Slippi online: ping %d ms, longest wait %d ms\n", mp_platform_slippi_net_ping_ms(),
              (int) on.longest_wait_ms, 0);
    }
}

/* ---- StartEngineLoop (801A4DE4), top of each engine body ---- */
void mp_slippi_online_frame_begin(void)
{
    int frame, offset, idx;

    if (!hooks_on()) {
        return;
    }
    frame = global_frame();
    if (on.load_logging && frame == 0) {
        load_mark("first frame (match set up)", 0);
    }
    if (on.load_logging && frame == 120) {
        load_mark("frame 120", 0);
        mp_platform_slippi_load_log(0);
        on.load_logging = 0;
    }
    mp_slippi_record_online_frame(frame);
    exchange_pads(frame);
    if (on.disconnected && !on.disconnect_shown && !on.game_over) {
        on.disconnect_shown = 1;
        mp_slippi_gmvs_end_online(on.remote_index);
    }
    {
        int v = frame > on.finalized ? on.finalized : frame;
        if (v > on.stable_finalized) {
            on.stable_finalized = v;
        }
    }
    offset = frame - (on.desync_last_frame + 1);
    idx = on.desync_write_idx;
    if (offset >= 0) {
        on.desync_write_idx = (u8) ((on.desync_write_idx + 1) % DESYNC_ENTRIES);
        on.desync_last_frame = frame;
    }
    idx = (idx + offset) % DESYNC_ENTRIES;
    if (idx < 0) {
        idx += DESYNC_ENTRIES;
    }
    on.desync_local[idx].frame = frame;
    on.desync_local[idx].checksum = compute_checksum();

    if (frame != 0 && !on.desync_shown) {
        int cf = mp_platform_slippi_net_remote_checksum_frame();
        u32 remote = mp_platform_slippi_net_remote_checksum_value();
        int k;
        if (cf > UNFREEZE_FRAME && cf <= on.stable_finalized) {
            for (k = 0; k < DESYNC_ENTRIES; k++) {
                u32 local;
                int diff;
                if (on.desync_local[k].frame != cf) {
                    continue;
                }
                local = on.desync_local[k].checksum;
                diff = (int) (s16) (local & 0xFFFF) - (int) (s16) (remote & 0xFFFF);
                if (diff < -1 || diff > 1 || (local >> 16) != (remote >> 16)) {
                    on.desync_shown = 1;
                    logf_("Slippi online: DESYNC at frame %d (ours %08X, theirs %08X)\n", cf, (int) local,
                          (int) remote);
                }
                break;
            }
        }
    }
    if (!on.game_over) {
        if (gmVs_GetController_0()->match_result == 0) {
            on.game_end_frame = 0;
        } else {
            if (on.game_end_frame == 0) {
                on.game_end_frame = frame;
            }
            if (frame - on.game_end_frame > 7) {
                on.game_over = 1;
                logf_("Slippi online: match over at frame %d\n", frame, 0, 0);
            }
        }
    }
}

/* ---- end of each engine body: catching up (Dolphin's shouldAdvanceOnlineFrame) ----
 * A lockstep peer never runs ahead, but it can fall behind: the PC loads a
 * match faster and keeps its frames up to 7 ahead of our inputs. Every 30
 * frames, when the opponent is more than 26.7 ms ahead, queue an extra pad
 * sample on some of the next frames, so the scene loop runs an extra engine
 * body (the draw just skips a frame): up to 3 extra frames, one every 5. */
void lb_8001955C(void);
void fn_800195FC(void);

void mp_slippi_online_frame_end(void)
{
    int frame;
    if (!hooks_on() || on.game_over || on.disconnected) {
        return;
    }
    frame = global_frame();
    /* Running ahead: Dolphin halts up to 5 frames when more than 10 ms ahead
     * on the first 120 frames, then only slows its clock, by up to 0.5 % when
     * more than 8 ms ahead. The 3DS drops pad samples instead: whole frames
     * early on, then at most one frame per 180 (about 0.55 %). The offset is
     * noisy (both 3DS peers of a test pair read about +12 ms), so later drops
     * stay rare. */
    if (frame % 30 == 0 && on.drop_samples == 0 && !on.advancing) {
        int offset = mp_platform_slippi_net_time_offset_us();
        int n = 0;
        if (frame <= 120 && offset > 10000) {
            n = (offset - 10000) / 16683 + 1;
            n = n > 5 ? 5 : n;
        } else if (frame > 120 && offset > 8000 && frame - on.last_drop_frame >= 180) {
            n = 1;
        }
        if (n > 0) {
            on.drop_samples = n;
            on.last_drop_frame = frame;
            if (on.dropped < 20 || frame % 600 == 0) {
                logf_("Slippi online: %d us ahead at frame %d; dropping %d frames\n", offset, frame, n);
            }
        }
    }
    /* As Dolphin: no advancing before frame 120; the PC's own start-up
     * stalls line the two games up first. */
    if (frame > 120 && frame % 30 == 0 && !on.advancing && on.drop_samples == 0) {
        int offset = mp_platform_slippi_net_time_offset_us();
        if (offset < -(16683 + 10000)) {
            int n = -offset / 16683;
            on.advance_left = n > 3 ? 3 : n;
            on.advance_gap = 0;
            on.advancing = on.advance_left > 0;
            if (on.advances < 20 || frame % 600 == 0) {
                logf_("Slippi online: %d us behind at frame %d; catching up %d frames\n", -offset, frame,
                      on.advance_left);
            }
        }
    }
    if (on.advancing) {
        if (on.advance_gap-- <= 0) {
            fn_800195FC();   /* one more pad sample: one more engine body */
            on.advances++;
            on.advance_gap = 4;
            if (--on.advance_left <= 0) {
                on.advancing = 0;
            }
        }
    }
}

/* ---- what the render pass does that gameplay reads ----
 * The PC draws every frame; a 3DS tick that is not followed by a draw still
 * needs the camera matrices (offscreen checks) and the fighters' bone
 * matrices (read raw by gameplay), as Slippi's rollback re-simulation runs
 * ExecCameraTasks between frames. */
void mp_slippi_tick_without_draw(void)
{
    HSD_GObj* gobj;
    if (!hooks_on() && !mp_slippi_replay_on()) {
        return;
    }
    if (Camera_80030A50() == NULL) {
        return;
    }
    Camera_8002A4AC(Camera_80030A50());
    HSD_CObjSetCurrent(Camera_80030A50()->hsd_obj);
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL; gobj = gobj->next) {
        Fighter* fp = gobj->user_data;
        if (!fp->x221F_b3 && ftLib_80086A8C(gobj) && fp->parts != NULL && fp->kind < 33) {
            u32 n = ftPartsTable[fp->kind]->parts_num, i;
            for (i = 0; i < n; i++) {
                HSD_JObj* j = fp->parts[i].joint;
                if (j != NULL) {
                    HSD_JObjSetupMatrix(j);
                }
            }
        }
    }
}

/* ---- match files read ahead (see mp_native_file_prefetch) ----
 * The lists are what a match load opens, measured with the open log on each
 * legal stage: files every match uses, each stage's own, and each fighter's
 * (model, animations, costumes, effects, sound bank). */
int mp_platform_file_id(const char* name);

static const char* const common_files[] = {
    "LbRb.dat", "EfMnData.dat", "EfCoData.dat", "LbRf.dat", "ItCo.usd", "PdPm.dat", "TyDatai.usd",
    "IfAll.usd", "PlCo.dat", "GmPause.usd", "SdIntro.dat", "IfCoGet.dat", "LbBf.dat", "audio/us/clink.ssm",
};
static const struct {
    int stage;
    const char* files[6];
} stage_files[] = {
    {2, {"GrIz.dat"}},
    {3, {"GrPs.usd", "audio/us/pstadium.ssm", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat"}},
    {8, {"GrSt.dat"}},
    {28, {"GrOp.dat", "audio/us/pupupu.ssm"}},
    {31, {"GrNBa.dat"}},
    {32, {"GrNLa.dat", "audio/us/last.ssm"}},
};
/* External character id: file code, effects code, sound bank. */
static const struct {
    const char* code;
    const char* effects;
    const char* bank;
} fighters[26] = {
    {"Ca", "Ca", "captain"}, {"Dk", "Dk", "dk"}, {"Fx", "Fx", "fox"}, {"Gw", NULL, "gw"},
    {"Kb", "Kb", "kirby"}, {"Kp", "Kp", "koopa"}, {"Lk", "Lk", "link"}, {"Lg", "Lg", "luigi"},
    {"Mr", "Mr", "mario"}, {"Ms", "Ms", "mars"}, {"Mt", "Mt", "mewtwo"}, {"Ns", "Ns", "ness"},
    {"Pe", "Pe", "peach"}, {"Pk", "Pk", "pikachu"}, {"Pp", "Ic", "ice"}, {"Pr", "Pr", "purin"},
    {"Ss", "Ss", "samus"}, {"Ys", "Ys", "yoshi"}, {"Zd", "Zd", "zs"}, {"Sk", "Zd", "zs"},
    {"Fc", "Fx", "falco"}, {"Cl", "Lk", "clink"}, {"Dr", "Mr", "drmario"}, {"Fe", "Fe", "emblem"},
    {"Pc", "Pk", "pichu"}, {"Gn", "Gn", "ganon"},
};
static const char* const costume_codes[] = {"Nr", "Re", "Bu", "Gr", "Ye", "Wh", "Bk", "Or", "La", "Pi", "Aq", "Gy"};

static void prefetch_name(const char* name)
{
    int id = mp_platform_file_id(name);
    if (id >= 0) {
        mp_platform_slippi_prefetch(id);
    }
}

static void prefetch_code(const char* code)
{
    char name[32];
    unsigned i;
    snprintf(name, sizeof name, "Pl%s.dat", code);
    prefetch_name(name);
    snprintf(name, sizeof name, "Pl%sAJ.dat", code);
    prefetch_name(name);
    for (i = 0; i < sizeof costume_codes / sizeof costume_codes[0]; i++) {
        snprintf(name, sizeof name, "Pl%s%s.dat", code, costume_codes[i]);
        prefetch_name(name);
    }
}

static void prefetch_fighter(int ext)
{
    char name[40];
    if (ext < 0 || ext >= 26) {
        return;
    }
    prefetch_code(fighters[ext].code);
    if (ext == 14) {
        prefetch_code("Nn");            /* Nana */
    } else if (ext == 18) {
        prefetch_code("Sk");            /* Zelda transforms into Sheik */
    } else if (ext == 19) {
        prefetch_code("Zd");
    }
    if (fighters[ext].effects != NULL) {
        snprintf(name, sizeof name, "Ef%sData.dat", fighters[ext].effects);
        prefetch_name(name);
    }
    snprintf(name, sizeof name, "audio/us/%s.ssm", fighters[ext].bank);
    prefetch_name(name);
}

static void prefetch_stage(int stage)
{
    unsigned i, k;
    for (i = 0; i < sizeof stage_files / sizeof stage_files[0]; i++) {
        if (stage_files[i].stage == stage) {
            for (k = 0; k < 6 && stage_files[i].files[k] != NULL; k++) {
                prefetch_name(stage_files[i].files[k]);
            }
        }
    }
}

/* Before matchmaking: the common files, this player's fighter and stage.
 * The opponent's fighter and stage follow once picked (14 MB budget). */
static void prefetch_ours(void)
{
    unsigned i;
    for (i = 0; i < sizeof common_files / sizeof common_files[0]; i++) {
        prefetch_name(common_files[i]);
    }
    prefetch_fighter(on.character);
    prefetch_stage(on.stage);
}

/* ---- matchmaking, before the match scene (the online CSS's job) ---- */

void mp_slippi_online_boot_mode(u8* mode)
{
    if (mp_slippi_online_configured() && !mp_slippi_replay_on()) {
        *mode = GM_DEBUG_VS;
    }
}

/* Blocks in the debug VS preload until both games agreed on the match. */
int mp_slippi_online_wait_match(void)
{
    int last_status = -1, polls = 0;
    if (!mp_slippi_online_configured() || mp_slippi_replay_on()) {
        return 0;
    }
    mp_platform_slippi_prefetch_pause(0);   /* waiting: read ahead */
    prefetch_ours();
    if (!on.started) {
        on.started = 1;
        logf_("Slippi online: looking for the opponent\n", 0, 0, 0);
        if (mp_platform_slippi_net_start(on.opponent) < 0) {
            mp_platform_log("Slippi online: could not start: ");
            mp_platform_log(mp_platform_slippi_net_error());
            mp_platform_log("\n");
            on.configured = -1;
            return 0;
        }
    } else {
        /* The next game. No reset here: the network layer already reset at
         * the last game's frame 1 (Dolphin's StartSlippiGame), and the
         * opponent's selections for this game may have arrived during the
         * results screen; clearing them would wait forever. */
        on.selections_sent = 0;
    }
    for (;;) {
        int status;
        mp_platform_slippi_net_poll();
        /* Sleep every pass (the network thread services the connection), so
         * the background file reader gets the CPU while we wait. */
        if ((polls & 15) == 0) {
            mp_platform_slippi_wait_poll();
        }
        mp_platform_idle();
        status = mp_platform_slippi_net_status();
        if (status != last_status) {
            last_status = status;
            logf_("Slippi online: status %d\n", status, 0, 0);
            if (status == 4 || status == 5) {
                /* Lost the opponent (or matchmaking failed): look again
                 * rather than falling through to the debug VS match. */
                mp_platform_log("Slippi online: ");
                mp_platform_log(mp_platform_slippi_net_error());
                mp_platform_log("; searching again\n");
                mp_platform_slippi_net_stop();
                for (int i = 0; i < 2000; i++) {
                    mp_platform_idle();
                }
                on.selections_sent = 0;
                if (mp_platform_slippi_net_start(on.opponent) < 0) {
                    mp_platform_log("Slippi online: could not restart: ");
                    mp_platform_log(mp_platform_slippi_net_error());
                    mp_platform_log("\n");
                }
                last_status = -1;
                continue;
            }
        }
        if (status == 3 && !on.selections_sent) {
            on.selections_sent = 1;
            mp_platform_slippi_net_set_selections(on.character, on.color, on.stage, on.stage_select);
            logf_("Slippi online: connected as player %d, ping %d ms; selections sent\n",
                  mp_platform_slippi_net_local_index() + 1, mp_platform_slippi_net_ping_ms(), 0);
        }
        if (status == 3) {
            /* The opponent's fighter, as soon as it is picked (kept for
             * rematches even when the match starts right away). */
            int remote = mp_platform_slippi_net_remote_character();
            if (remote >= 0 && remote != on.prefetched_remote) {
                on.prefetched_remote = remote;
                prefetch_fighter(remote);
            }
        }
        if (status == 3 && mp_platform_slippi_net_match_block(on.block)) {
            on.pending = 1;
            mp_platform_slippi_prefetch_pause(1);   /* the match load needs the SD card */
            logf_("Slippi load: %d files (%d KB) read ahead before the match\n",
                  (int) mp_platform_slippi_prefetch_stat(0), (int) mp_platform_slippi_prefetch_stat(1), 0);
            logf_("Slippi load: read-ahead queue %d, taken %d, step %d\n", (int) mp_platform_slippi_prefetch_stat(2),
                  (int) mp_platform_slippi_prefetch_stat(3), (int) mp_platform_slippi_prefetch_stat(4));
            load_mark("match ready", 1);
            mp_platform_slippi_load_log(1);
            on.load_logging = 1;
            logf_("Slippi online: match ready, stage %d\n", (on.block[0xE] << 8) | on.block[0xF], 0, 0);
            return 1;
        }
        if (++polls % 10000 == 0) {
            logf_("Slippi online: still waiting (status %d)\n", status, 0, 0);
        }
    }
}

const unsigned char* mp_slippi_online_pending_block(void)
{
    return on.pending ? on.block : NULL;
}

/* ---- InitOnlinePlay (8016E748) ---- */

static void sync_rng_proc(HSD_GObj* gobj)
{
    u32 f = (u32) global_frame();
    (void) gobj;
    *seed_ptr = ((f << 16) | (f >> 16)) + on.rng_offset;
}

void mp_slippi_online_start_melee(StartMeleeData* data)
{
    HSD_GObj* gobj;
    int i, delay;
    if (!on.pending) {
        return;
    }
    on.pending = 0;
    memcpy(data, on.block, BLOCK_SIZE);
    memset((u8*) data + 0x38, 0, 0x5C - 0x38);
    on.rng_offset = be32(on.block + BLOCK_SIZE);
    on.local_index = on.block[BLOCK_SIZE + 4] & 3;
    on.remote_index = on.local_index == 0 ? 1 : 0;
    /* Online-only gameplay codes (LGL, GAME! hold) and the frozen Stadium choice. */
    mp_slippi_rules_set_online(2 /* DIRECT */, on.local_index);
    mp_slippi_rules_set_frozen_stadium(on.block[BLOCK_SIZE + 6]);
    delay = on.block[BLOCK_SIZE + 5];
    if (delay < MIN_DELAY) {
        delay = on.delay_setting;
    }
    if (delay < MIN_DELAY) {
        delay = MIN_DELAY;
    }
    if (delay > MAX_DELAY) {
        delay = MAX_DELAY;
    }
    on.delay = (u8) delay;
    on.frame = 1;
    on.delay_index = 0;
    memset(on.delay_buffer, 0, sizeof on.delay_buffer);
    memset(on.last_local, 0, sizeof on.last_local);
    memset(on.desync_local, 0, sizeof on.desync_local);
    on.finalized = on.stable_finalized = 0;
    on.tx_checksum = 0;
    on.desync_last_frame = 0;
    on.desync_write_idx = 0;
    on.disconnected = on.disconnect_shown = on.desync_shown = on.game_over = on.game_end_frame = 0;
    on.skips = on.skip_run = on.frames = on.waits = on.wait_ms = on.longest_wait_ms = on.no_sample_bodies = 0;
    on.advance_left = on.advance_gap = on.advancing = on.drop_samples = on.last_drop_frame = 0;
    on.advances = on.dropped = 0;
    *seed_ptr = on.rng_offset;
    /* No transformation from a held A. */
    for (i = 0; i < 4; i++) {
        HSD_PadCopyStatus[i].button = 0;
    }
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, sync_rng_proc, 0);
    on.active = 1;
    load_mark("scene preload done, match setup starting", 0);
    if (on.record) {
        mp_slippi_record_online_begin();
    }
    logf_("Slippi online: match starts, local port %d, delay %d, rng offset %08X\n", on.local_index + 1,
          on.delay, (int) on.rng_offset);
}

void mp_slippi_online_match_exit(void)
{
    if (!on.active) {
        return;
    }
    mp_slippi_record_online_end();
    mp_slippi_rules_set_online(-1, 0);
    logf_("Slippi online: match exit after %d frames, %d waits for the opponent, desync %d\n", (int) on.frames,
          (int) on.waits, on.desync_shown);
    logf_("Slippi online: waited %d ms in total, longest %d ms\n", (int) on.wait_ms, (int) on.longest_wait_ms, 0);
    logf_("Slippi online: %d extra frames run to catch up, %d dropped to wait for the opponent\n",
          (int) on.advances, (int) on.dropped, 0);
    on.active = 0;
}

int mp_slippi_online_active(void)
{
    return on.active;
}
