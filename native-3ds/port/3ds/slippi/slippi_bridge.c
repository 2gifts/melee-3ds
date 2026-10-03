/* Native side of the engine bridge (port/include/slippi_net_bridge.h).
 * mp_platform_slippi_net_X (engine, big-endian) -> slippi_net_X (here). */
#include "slippi_internal.h"
#include <stdio.h>
#include <string.h>

/* Lockstep frame state (CEXISlippi members used by handleOnlineInputs). */
static int stall_frames;
static uint64_t stall_since_us;   /* when the current wait for a remote pad began */
static int frames_to_skip, is_currently_skipping;
static char error_text[160];

int slippi_net_start(const char *opponent_code)
{
    int rc = slippi_init();
    if (rc != 0) return rc;
    stall_frames = frames_to_skip = is_currently_skipping = 0;
    return slippi_find_match(opponent_code);
}

int slippi_net_status(void)
{
    switch (slippi_status()) {
        case SLIPPI_STATUS_SEARCHING: return 1;
        case SLIPPI_STATUS_CONNECTING: return 2;
        case SLIPPI_STATUS_CONNECTED: return 3;
        case SLIPPI_STATUS_FAILED: return 4;
        case SLIPPI_STATUS_DISCONNECTED: return 5;
        case SLIPPI_STATUS_UNINITIALIZED: return g_slippi.error[0] ? 4 : 0;
        default: return 0;
    }
}

const char *slippi_net_error(void)
{
    slippi_error(error_text, sizeof(error_text));
    return error_text;
}

void slippi_net_poll(void) { slippi_poll(); }
void slippi_net_stop(void) { slippi_shutdown(); }

int slippi_net_local_index(void) { return g_slippi.match.is_host ? 0 : 1; }

void slippi_net_set_selections(int char_id, int color, int stage_id, int stage_selected)
{
    slippi_set_selections(char_id, color, stage_selected ? stage_id : -1, 0);
}

int slippi_net_remote_ready(void)
{
    unsigned char s[13];
    slippi_remote_selections(s);
    return s[2] != 0;
}

/* Bytes 8 and 9 are 0 as in Slippi Ishiiruka (what Slippi Launcher ships;
 * mainline has 1, 1 there): the PC's replays record 0 at 0x9. */
/* CEXISlippi::prepareOnlineMatchState's static block: "a VS match with P1 Red
 * Falco vs P2 Red Bowser vs P3 Young Link vs P4 Young Link on Battlefield". */
static const unsigned char match_block_template[0x138] = {
    0x32, 0x01, 0x86, 0x4C, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x6E, 0x00,
    0x1F, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x3F,
    0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x78, 0x00, 0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x05, 0x00, 0x04,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00, 0xC0, 0x00, 0x04, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F,
    0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00,
    0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x09, 0x00, 0x78, 0x00, 0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
    0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00, 0x40, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09,
    0x00, 0x78, 0x00, 0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
};

/* Builds the DIRECT 1v1 block exactly as prepareOnlineMatchState does once
 * local and remote are ready (no desync recovery, no overwrite selections).
 * Starting from the pristine template each time gives the same bytes as
 * Dolphin's persistent static for 1v1 Direct: every field it writes is
 * rewritten here, the rest never changes. Lock held. */
static void build_match_block(unsigned char *b, unsigned *rng_out, unsigned *alt_out)
{
    slippi_state *g = &g_slippi;
    memcpy(b, match_block_template, sizeof(match_block_template));
    const slippi_selections *lps = &g->local_sel, *rps = &g->remote_sel;
    const slippi_selections *ordered[2] = {NULL, NULL};
    if (lps->player_idx < 2) ordered[lps->player_idx] = lps;
    if (rps->player_idx < 2) ordered[rps->player_idx] = rps;

    /* Stage: first player in port order with a stage selection, else Battlefield. */
    unsigned stage_id = 0x1F, alt_stage_mode = 0;
    for (int i = 0; i < 2; ++i) {
        if (!ordered[i] || !ordered[i]->is_stage_selected) continue;
        stage_id = ordered[i]->stage_id;
        alt_stage_mode = ordered[i]->alt_stage_mode;
        break;
    }
    unsigned rng_offset = g->match.is_host ? lps->rng_offset : rps->rng_offset;

    /* Characters (team_id is 0 outside Teams). */
    for (int i = 0; i < 2; ++i) {
        const slippi_selections *s = ordered[i];
        if (!s || !s->is_character_selected) continue;
        b[0x60 + s->player_idx * 0x24] = s->character_id;
        b[0x63 + s->player_idx * 0x24] = s->character_color;
        b[0x67 + s->player_idx * 0x24] = 0;
        b[0x69 + s->player_idx * 0x24] = 0;
    }
    /* Shades for same character + colour (Sheik counts as Zelda). */
    unsigned keys[4], counts[4];
    int nkeys = 0;
    for (int i = 0; i < 4; ++i) {
        if (b[0x61 + i * 0x24] != 0) continue;
        unsigned char char_id = b[0x60 + i * 0x24];
        unsigned char color = b[0x63 + i * 0x24];
        if (char_id == 0x13) char_id = 0x12;
        unsigned key = ((unsigned)char_id << 8) | color;
        int k = 0;
        while (k < nkeys && keys[k] != key) ++k;
        if (k == nkeys) { keys[nkeys] = key; counts[nkeys] = 0; ++nkeys; }
        b[0x67 + 0x24 * i] = (unsigned char)counts[k];
        counts[k] += 1;
    }
    b[0x8] = 0;                       /* not Teams */
    b[0x61 + 2 * 0x24] = 3;           /* p3/p4: no player */
    b[0x61 + 3 * 0x24] = 3;
    b[0xE] = (unsigned char)(stage_id >> 8);
    b[0xF] = (unsigned char)stage_id;
    b[2] &= 0xF7;                     /* DIRECT: vanilla pause allowed */
    /* Desync-recovery defaults: 480 s, 4 stocks, 0 %. */
    b[0x10] = 0; b[0x11] = 0; b[0x12] = 480 >> 8; b[0x13] = 480 & 0xFF;
    for (int i = 0; i < 4; ++i) {
        b[0x62 + i * 0x24] = 4;
        b[0x70 + i * 0x24] = 0;
        b[0x71 + i * 0x24] = 0;
    }
    /* Mode, timer, items (not Party). */
    b[0x0] = 0x32;
    b[0x3] = 0x4C;
    b[0x10] = 0; b[0x11] = 0; b[0x12] = 480 >> 8; b[0x13] = 480 & 0xFF;
    b[0xB] = 0xFF;
    unsigned long long items = 0xF80000000F000000ull;
    if (g->match.items) { items |= (unsigned long long)g->match.items << 28; b[0xB] = 3; }
    for (int i = 0; i < 8; ++i) b[0x23 + i] = (unsigned char)(items >> (56 - 8 * i));
    *rng_out = rng_offset;
    *alt_out = alt_stage_mode;
}

static uint32_t crc32_bytes(const unsigned char *p, int n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

int slippi_net_match_block(unsigned char *out)
{
    slippi_state *g = &g_slippi;
    sp_lock();
    int ready = g->status == SLIPPI_STATUS_CONNECTED && g->local_sel.is_character_selected && g->remote_sel.is_character_selected;
    if (ready && out) {
        unsigned rng, alt;
        build_match_block(out, &rng, &alt);
        out[0x138] = (unsigned char)(rng >> 24);
        out[0x139] = (unsigned char)(rng >> 16);
        out[0x13A] = (unsigned char)(rng >> 8);
        out[0x13B] = (unsigned char)rng;
        out[0x13C] = (unsigned char)(g->match.is_host ? 0 : 1);
        out[0x13D] = (unsigned char)g->cfg.delay;
        out[0x13E] = (unsigned char)alt;
        out[0x13F] = 0;
        sp_log("match block crc32 0x%08x: stage 0x%02x%02x, p1 char %u color %u, p2 char %u color %u, rng 0x%x, local index %u, delay %u",
               (unsigned)crc32_bytes(out, 0x138), out[0xE], out[0xF], out[0x60], out[0x63], out[0x84], out[0x87], rng, out[0x13C], out[0x13D]);
    }
    sp_unlock();
#ifdef __3DS__
    if (ready && out) {
        /* The stage may be the opponent's pick: cache what it loads mid-match. */
        extern void slippi_prefetch_stage_extras(int stage);
        slippi_prefetch_stage_extras((out[0xE] << 8) | out[0xF]);
    }
#endif
    return ready;
}

void slippi_net_new_game(void)
{
    slippi_start_game();
    sp_lock();
    stall_frames = frames_to_skip = is_currently_skipping = 0;
    sp_unlock();
}

/* CEXISlippi::shouldSkipOnlineFrame for a peer that never predicts: the
 * rollback-limit halt becomes "remote input for this frame not here yet". */
static int should_skip(int frame, int *disconnect)
{
    slippi_state *g = &g_slippi;
    *disconnect = 0;
    if (slippi_remote_pad_locked(frame, NULL) != 1) {
        /* Dolphin's 420-frame rule, by time: the engine may retry a frame many
         * times per 16.7 ms while it waits. */
        if (stall_frames++ == 0) stall_since_us = sp_time_us();
        if (sp_time_us() - stall_since_us > 7000000ull) {
            sp_log("force-disconnecting after a 7 s stall (frame %d, latest remote %d)", frame, (int)g->remote_head_frame);
            g->disconnect_reason = 0;
            slippi_p2p_disconnect();
            slippi_set_status(SLIPPI_STATUS_DISCONNECTED);
            snprintf(g->error, sizeof(g->error), "Opponent stopped sending inputs");
            stall_frames = 0;
            *disconnect = 1;
        }
        return 1;
    }
    stall_frames = 0;
    const int frame_time = SLIPPI_FRAME_US, t1 = 10000;
    if (frame % SLIPPI_LOCKSTEP_INTERVAL == 0 && !is_currently_skipping && frame <= 120) {
        int offset_us = slippi_p2p_time_offset();
        if (offset_us > t1) {
            is_currently_skipping = 1;
            int max_skip_frames = 5;
            frames_to_skip = (offset_us - t1) / frame_time + 1;
            if (frames_to_skip > max_skip_frames) frames_to_skip = max_skip_frames;
            sp_log("halting on frame %d due to time sync: offset %d us, %d frames", frame, offset_us, frames_to_skip);
        }
    }
    if (frames_to_skip > 0) { --frames_to_skip; return 1; }
    is_currently_skipping = 0;
    return 0;
}

int slippi_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum, const unsigned char *local_pad12,
                           unsigned char *remote_pad12_out)
{
    slippi_state *g = &g_slippi;
    sp_lock();
    if (g->status != SLIPPI_STATUS_CONNECTED) { sp_unlock(); return 3; }
    /* Dolphin runs StartSlippiGame on every frame-1 call; nothing is queued yet then. */
    /* A new game: frame 1 again (the first call queues frames 1..1+delay,
     * so retries of frame 1 see last_queued_frame == 1 + delay). The old
     * test, last_queued_frame == 0, only held for the first game. */
    if (frame == 1 && g->last_queued_frame != 1 + delay) {
        slippi_start_game_locked();
        stall_frames = frames_to_skip = is_currently_skipping = 0;
    }
    /* Our pad for this frame goes out before we wait for the opponent's,
     * once per frame (retries of the same frame only re-send). Waiting first
     * deadlocks two lockstep peers (3DS vs 3DS); Dolphin never waits. */
    int cf = g->cfg.send_checksum ? finalized_frame : 0;
    unsigned ck = g->cfg.send_checksum ? checksum : 0;
    int queued = 0;
    if (frame + delay > g->last_queued_frame) {
        slippi_send_pad_locked(frame + delay, local_pad12, cf, ck);
        queued = 1;
    }
    int disconnect;
    if (should_skip(frame, &disconnect)) {
        /* Retries arrive about every millisecond while the engine waits; the
         * network thread re-sends un-acked pads every 16.7 ms (auto_resend). */
        sp_unlock();
        return disconnect ? 3 : 2;
    }
    if (!queued) slippi_p2p_send_pads();
    if (remote_pad12_out) {
        slippi_remote_pad_locked(frame, remote_pad12_out);
        memset(remote_pad12_out + 8, 0, 4);
    }
    sp_unlock();
    return 1;
}

int slippi_net_remote_checksum(int *unused) { (void)unused; return slippi_remote_checksum_frame(); }
int slippi_net_remote_checksum_frame(void) { return slippi_remote_checksum_frame(); }
unsigned slippi_net_remote_checksum_value(void) { return slippi_remote_checksum(); }
int slippi_net_ping_ms(void) { return slippi_ping_us() / 1000; }
/* The opponent's character once they picked one (for prefetching), else -1. */
int slippi_net_remote_character(void)
{
    slippi_state *g = &g_slippi;
    sp_lock();
    int c = g->remote_sel.is_character_selected ? (int)g->remote_sel.character_id : -1;
    sp_unlock();
    return c;
}

/* Dolphin's CalcTimeOffsetUs: positive when we are ahead of the opponent. */
int slippi_net_time_offset_us(void)
{
    sp_lock();
    int v = slippi_p2p_time_offset();
    sp_unlock();
    return v;
}
