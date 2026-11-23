/* Slippi's side of local wireless play: an adapter between the engine bridge (slippi_bridge.c, slippi_net.c) and
 * the game-independent localwireless module (port/3ds/localwireless). The module moves bytes: one 8-byte pad per
 * console per frame, a 13-byte "selections" setup per console, and the match block as the start blob. Everything
 * Melee-specific stays here: the selections layout, which fields are required, the CPU takeover (engine side).
 *
 * config.ini: opponent=@host (this console creates the game and is player 1) or opponent=@join (finds a game
 * nearby and is told its player number). local_players=2..4 on the host sets how many consoles it waits for.
 * All functions run with the global lock held. */
#include "slippi_internal.h"
#include "localwireless.h"
#include "lw_platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- what the module needs from us ---- */
uint64_t lw_platform_time_us(void) { return sp_time_us(); }
uint32_t lw_platform_random(void) { return sp_random(); }
int lw_platform_net_init(void) { return sp_net_init(); }
void lw_platform_log(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sp_log("%s", line);
}

#define PAD_BYTES 8
#define SELECTIONS_BYTES 13

void slippi_local_start(int host)
{
    slippi_state *g = &g_slippi;
    char err[160] = "";
    slippi_local_close();
    slippi_p2p_clear();
    memset(&g->match, 0, sizeof(g->match));
    g->match.is_host = g->local_host = host;
    g->match.local_index = host ? 0 : -1;       /* a client learns its player index from the host */
    g->match.player_count = host ? g->cfg.local_players : 0;
    g->local_sel.player_idx = (uint8_t)(host ? 0 : 1);
    memset(g->stats, 0, sizeof(g->stats));
    g->error[0] = '\0';
    lw_config c;
    memset(&c, 0, sizeof(c));
    c.host = host;
    c.players = g->cfg.local_players;
    c.input_size = PAD_BYTES;
    c.frame_us = SLIPPI_FRAME_US;
    c.timeout_ms = g->cfg.local_timeout_ms;
#if defined(__3DS__) && !defined(MP_SMOKE_TEST)
    c.use_udp = 0;                               /* release 3DS builds only use UDS */
#elif defined(__3DS__)
    c.use_udp = g->cfg.local_udp;                /* development build: the loopback link for emulator tests */
#else
    c.use_udp = 1;
#endif
    c.port = g->cfg.local_port;
    c.test_drop_pct = g->cfg.test_drop_pct;
    sp_log("local: %s", host ? "creating a game" : "looking for a game");
    if (lw_open(&c, err, sizeof(err)) != 0) {
        slippi_fail("%s", err[0] ? err : "Local wireless is unavailable");
        return;
    }
    snprintf(g->user.display_name, sizeof(g->user.display_name), "%s", lw_local_name());
    slippi_set_status(SLIPPI_STATUS_SEARCHING);
}

void slippi_local_close(void) { lw_close(); }

void slippi_local_service(void)
{
    slippi_state *g = &g_slippi;
    if (g->status != SLIPPI_STATUS_SEARCHING && g->status != SLIPPI_STATUS_CONNECTED) return;
    int st = lw_poll();
    if (st == LW_FAILED) {
        slippi_fail("%s", lw_error()[0] ? lw_error() : "Local wireless failed");
        return;
    }
    if (st == LW_DISCONNECTED) {
        snprintf(g->error, sizeof(g->error), "%s", lw_error());
        slippi_set_status(SLIPPI_STATUS_DISCONNECTED);
        return;
    }
    if (g->status == SLIPPI_STATUS_SEARCHING && st == LW_CONNECTED) {
        g->match.local_index = lw_my_index();
        g->match.player_count = lw_players();
        g->local_sel.player_idx = (uint8_t)g->match.local_index;
        snprintf(g->match.remote_name, sizeof(g->match.remote_name), "%s", lw_player_name(g->match.is_host ? 1 : 0));
        g->match.remote_code[0] = 0;
        g->p2p_connected = 1;
        g->last_receive_us = sp_time_us();
        slippi_set_status(SLIPPI_STATUS_CONNECTED);
        sp_log("local: connected; we are player %d of %d%s", g->match.local_index + 1, g->match.player_count, g->match.is_host ? " (host)" : "");
    }
}

/* ---- selections: 13 bytes, as the 1v1 selections packet without its id ---- */

static void encode_selections(const slippi_selections *s, uint8_t out[SELECTIONS_BYTES])
{
    out[0] = s->character_id;
    out[1] = s->character_color;
    out[2] = s->is_character_selected;
    out[3] = s->player_idx;
    out[4] = (uint8_t)(s->stage_id >> 8);
    out[5] = (uint8_t)s->stage_id;
    out[6] = s->is_stage_selected;
    out[7] = (uint8_t)(s->rng_offset >> 24);
    out[8] = (uint8_t)(s->rng_offset >> 16);
    out[9] = (uint8_t)(s->rng_offset >> 8);
    out[10] = (uint8_t)s->rng_offset;
    out[11] = s->team_id;
    out[12] = s->alt_stage_mode;
}

void slippi_local_publish_selections(void)
{
    uint8_t b[SELECTIONS_BYTES];
    encode_selections(&g_slippi.local_sel, b);
    lw_set_setup(b, sizeof(b));
    sp_log("local: selections char %u color %u sel %u stage %u sel %u rng 0x%x", b[0], b[1], b[2], g_slippi.local_sel.stage_id, b[6],
           (unsigned)g_slippi.local_sel.rng_offset);
}

/* Host: 1 and every console's selections once all of them have picked a fighter. */
int slippi_local_host_ready(slippi_selections sel[4])
{
    if (!g_slippi.match.is_host || g_slippi.status != SLIPPI_STATUS_CONNECTED) return 0;
    for (int i = 0; i < lw_players(); ++i) {
        uint8_t b[SELECTIONS_BYTES];
        if (lw_get_setup(i, b, sizeof(b)) < SELECTIONS_BYTES) return 0;
        slippi_selections *s = &sel[i];
        memset(s, 0, sizeof(*s));
        s->character_id = b[0];
        s->character_color = b[1];
        s->is_character_selected = b[2];
        s->player_idx = (uint8_t)i;
        s->stage_id = (uint16_t)((b[4] << 8) | b[5]);
        s->is_stage_selected = b[6];
        s->rng_offset = ((uint32_t)b[7] << 24) | ((uint32_t)b[8] << 16) | ((uint32_t)b[9] << 8) | b[10];
        s->team_id = b[11];
        s->alt_stage_mode = b[12];
        if (!s->is_character_selected) return 0;
    }
    return 1;
}

int slippi_local_host_start(const unsigned char *blk) { return lw_host_start(blk, 0x140); }
int slippi_local_start_blob(unsigned char *out, int cap) { return lw_get_start(out, cap); }
void slippi_local_game_begin(void) { lw_game_begin(); }

/* ---- the match ---- */

int slippi_local_submit(int frame, const unsigned char *pad8, int cf, unsigned ck)
{
    lw_submit(frame, pad8, cf, ck);
    return 0;
}

int slippi_local_bundle(int frame, unsigned char pads[4][8], unsigned char *cpu_mask)
{
    uint8_t in[LW_MAX_PLAYERS][LW_MAX_INPUT];
    if (!lw_bundle(frame, in, cpu_mask)) return 0;
    for (int i = 0; i < 4; ++i) memcpy(pads[i], in[i], PAD_BYTES);
    return 1;
}

int slippi_local_checksum(unsigned *value)
{
    uint32_t ck = 0;
    int32_t f = lw_remote_checksum(&ck);
    if (value) *value = ck;
    return f;
}

int slippi_local_ping_us(void) { return lw_ping_us(); }
int slippi_local_players(void) { return lw_players(); }
const char *slippi_local_player_name(int idx) { return lw_player_name(idx); }

int slippi_local_stat(int which)
{
    switch (which) {
        case SLIPPI_STAT_PAD_PACKETS_SENT: return (int)lw_stat(LW_STAT_PACKETS_SENT);
        case SLIPPI_STAT_PAD_PACKETS_RECEIVED: return (int)lw_stat(LW_STAT_PACKETS_RECEIVED);
        case SLIPPI_STAT_PAD_PACKETS_STALE: return (int)lw_stat(LW_STAT_PACKETS_STALE);
        case SLIPPI_STAT_PAD_GAPS: return (int)lw_stat(LW_STAT_GAPS);
        case SLIPPI_STAT_ACKS_SENT: return (int)lw_stat(LW_STAT_ACKS_SENT);
        case SLIPPI_STAT_ACKS_RECEIVED: return (int)lw_stat(LW_STAT_ACKS_RECEIVED);
        case SLIPPI_STAT_RESENDS: return (int)lw_stat(LW_STAT_RESENDS);
        case SLIPPI_STAT_DROPPED_TEST: return (int)lw_stat(LW_STAT_DROPPED_TEST);
        default: return 0;
    }
}
