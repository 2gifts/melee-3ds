/* Engine-facing Slippi network API (Direct 1v1, lockstep).
 *
 * The engine (big-endian) calls these through SDKBRIDGE / SDKBRIDGE6 entries in
 * port/3ds/game_bridge.S; the native side is port/3ds/slippi/slippi_bridge.c
 * (names without the mp_platform_ prefix: slippi_net_*). Only scalars, byte
 * buffers and NUL-terminated strings cross; multi-byte values inside buffers
 * are big-endian.
 */
#ifndef SLIPPI_NET_BRIDGE_H
#define SLIPPI_NET_BRIDGE_H

/* Status codes (mp_platform_slippi_net_status). */
#define SLIPPI_NET_IDLE 0
#define SLIPPI_NET_MATCHMAKING 1
#define SLIPPI_NET_CONNECTING 2
#define SLIPPI_NET_CONNECTED 3
#define SLIPPI_NET_FAILED 4
#define SLIPPI_NET_DISCONNECTED 5

/* send_inputs results */
#define SLIPPI_NET_INPUTS_OK 1
#define SLIPPI_NET_INPUTS_SKIP 2
#define SLIPPI_NET_INPUTS_DISCONNECTED 3

/* Game info block + trailer (mp_platform_slippi_net_match_block). */
#define SLIPPI_NET_MATCH_BLOCK_SIZE 0x138
#define SLIPPI_NET_MATCH_BLOCK_TOTAL (0x138 + 8)

/* lifecycle */
/* Loads sdmc:/3ds/melee/slippi/user.json + config.ini, socInit, starts Direct
 * matchmaking for opponent_code ("ABCD#123"; NULL/"" = config opponent=).
 * 0 ok, <0 error (see mp_platform_slippi_net_error). */
int mp_platform_slippi_net_start(const char *opponent_code);
int mp_platform_slippi_net_status(void);
const char *mp_platform_slippi_net_error(void);   /* static NUL-terminated text */
void mp_platform_slippi_net_poll(void);           /* every tick and while waiting */
void mp_platform_slippi_net_stop(void);

/* match setup (after connected) */
/* 0 or 1: decider (MM isHost) = 0, like Dolphin's m_local_player_idx. */
int mp_platform_slippi_net_local_index(void);
/* Sends 0x82 with is_character_selected=1, team 0, alt_stage_mode 0 and a
 * fresh rng_offset = rand % 0xFFFF; stage_selected=0 leaves the stage to the
 * opponent (Battlefield if nobody picks). */
void mp_platform_slippi_net_set_selections(int char_id, int color, int stage_id, int stage_selected);
/* 1 once the remote 0x82 had is_character_selected. */
int mp_platform_slippi_net_remote_ready(void);
/* 1 when both sides are ready: out[0..0x137] = game info block built like
 * Dolphin's CEXISlippi::prepareOnlineMatchState (DIRECT 1v1), then
 * out[0x138..0x13B] = rng_offset (decider's, BE), out[0x13C] = local index,
 * out[0x13D] = input delay (config delay=), out[0x13E] = alt_stage_mode,
 * out[0x13F] = 0. 0 when not ready (out untouched). */
int mp_platform_slippi_net_match_block(unsigned char *out);
/* Resets pad queues/frame state for the next game (Dolphin StartSlippiGame).
 * send_inputs also does this itself at frame 1. */
void mp_platform_slippi_net_new_game(void);

/* per frame (frames start at 1 each game) */
/* Like CEXISlippi::handleOnlineInputs for a lockstep peer:
 *  1 OK:   pad for 'frame' queued tagged frame+delay (zero pads for 1..delay
 *          at frame 1) and sent with every un-acked pad; header checksum is
 *          (finalized_frame, checksum) unless config send_checksum=0;
 *          remote_pad12_out = remote pad for 'frame' (8 wire bytes + 4 zero).
 *  2 SKIP: remote pad for 'frame' missing, or Dolphin's time-sync halt (frames
 *          <= 120, ahead > 10 ms). No new local pad; un-acked pads are re-sent
 *          (Dolphin's SendSlippiPad(nullptr)). Retry the same frame next tick.
 *  3 DISCONNECTED (also after 420 consecutive skips, Dolphin's 7 s stall rule). */
int mp_platform_slippi_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum,
                                       const unsigned char *local_pad12, unsigned char *remote_pad12_out);
/* Legacy: returns the remote checksum frame; the pointer is ignored. */
int mp_platform_slippi_net_remote_checksum(int *unused_must_be_null);
int mp_platform_slippi_net_remote_checksum_frame(void);
unsigned mp_platform_slippi_net_remote_checksum_value(void);
int mp_platform_slippi_net_ping_ms(void);
/* The opponent's character id once selected, else -1. */
int mp_platform_slippi_net_remote_character(void);
/* Time offset (Dolphin CalcTimeOffsetUs): positive = we are ahead. */
int mp_platform_slippi_net_time_offset_us(void);

/* The Direct menu flow (port/3ds/slippi/slippi_ui.c, names without mp_platform_).
 * ui_css: packed = ready | ckind << 8 | color << 16; trigger/held = the local
 * port's HSD buttons. Returns flags: 1 locked in, 2 start the match, 4 go to
 * the SSS; 0x100/0x200/0x400/0x800 = forward/back/error/move sound.
 * ui_event: 1 CSS entered, 2 left to the menus, 3 stage picked (stage |
 * alt << 16, -1 = none), 4 game result (1 won), 5 match scene.
 * ui_remote: the opponent's character | colour << 8 once known, else -1. */
int mp_platform_slippi_ui_css(int packed, int trigger, int held);
void mp_platform_slippi_ui_event(int event, int arg);
int mp_platform_slippi_ui_remote(void);
/* Top-screen CSS text line (0-18, see slippi_ui.c) into buf as SIS-ready bytes;
 * returns its colour: 0 white, 1 gray, 2 red, 3 done (green), 4 waiting (blue). */
int mp_platform_slippi_ui_text(int line, char* buf, int len);

#endif
