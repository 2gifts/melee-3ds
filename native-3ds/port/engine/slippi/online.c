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
    int character, color, stage, stage_select, delay_setting;
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

int mp_slippi_online_pad_renew(PADStatus* stat)
{
    (void) stat;
    return 0;
}

void mp_platform_idle(void);

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
        mp_platform_slippi_net_new_game();
        on.selections_sent = 0;
    }
    for (;;) {
        int status;
        mp_platform_slippi_net_poll();
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
        if (status == 3 && mp_platform_slippi_net_match_block(on.block)) {
            on.pending = 1;
            logf_("Slippi online: match ready, stage %d\n", (on.block[0xE] << 8) | on.block[0xF], 0, 0);
            return 1;
        }
        if (++polls % 600000 == 0) {
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
    *seed_ptr = on.rng_offset;
    /* No transformation from a held A. */
    for (i = 0; i < 4; i++) {
        HSD_PadCopyStatus[i].button = 0;
    }
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, sync_rng_proc, 0);
    on.active = 1;
    mp_slippi_record_online_begin();
    logf_("Slippi online: match starts, local port %d, delay %d, rng offset %08X\n", on.local_index + 1,
          on.delay, (int) on.rng_offset);
}

void mp_slippi_online_match_exit(void)
{
    if (!on.active) {
        return;
    }
    mp_slippi_record_online_end();
    logf_("Slippi online: match exit after %d frames, %d waits for the opponent, desync %d\n", (int) on.frames,
          (int) on.waits, on.desync_shown);
    logf_("Slippi online: waited %d ms in total, longest %d ms\n", (int) on.wait_ms, (int) on.longest_wait_ms, 0);
    on.active = 0;
}

int mp_slippi_online_active(void)
{
    return on.active;
}
