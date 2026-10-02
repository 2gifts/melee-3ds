/* Slippi Online (Direct mode) network client for the 3DS port.
 *
 * Wire-compatible with Slippi Dolphin's SlippiMatchmaking / SlippiNetplayClient
 * (ENet 1.3.x over UDP, 3 channels; JSON matchmaking; SFML-style big-endian
 * netplay packets). See docs/slippi/network.md and docs/slippi/protocol_notes.md.
 *
 * Threading: every function is safe to call from the engine thread. By default
 * a background network thread services ENet every ~2 ms (config net_thread);
 * slippi_poll() also services it, so a driver can poll while it waits for a
 * remote input. Nothing here blocks except slippi_init() (socInit) and, with
 * net_thread=0, the DNS lookup inside slippi_poll().
 *
 * Only scalars, pointers to byte buffers and NUL-terminated strings cross this
 * API, so the big-endian engine can call it through SDKBRIDGE
 * (port/engine/slippi/slippi_net_engine.h). Every function takes at most four
 * arguments for the same reason.
 */
#ifndef SLIPPI_NET_H
#define SLIPPI_NET_H

#ifdef __cplusplus
extern "C" {
#endif

enum slippi_status_code {
    SLIPPI_STATUS_IDLE = 0,          /* initialised, not searching */
    SLIPPI_STATUS_SEARCHING = 1,     /* resolving / connecting to MM / waiting for an opponent */
    SLIPPI_STATUS_CONNECTING = 2,    /* opponent found, P2P ENet handshake in progress */
    SLIPPI_STATUS_CONNECTED = 3,     /* P2P connection up: selections and pads flow */
    SLIPPI_STATUS_FAILED = 4,        /* see slippi_error() */
    SLIPPI_STATUS_DISCONNECTED = 5,  /* was connected; the peer left or timed out */
    SLIPPI_STATUS_UNINITIALIZED = 6
};

/* Loads sdmc:/3ds/melee/slippi/user.json and config.ini, starts sockets/ENet and
 * the network thread. Idempotent. 0 on success, negative on error (slippi_error). */
int slippi_init(void);
void slippi_shutdown(void);

/* Starts a Direct search for the opponent's connect code ("ABCD#123", ASCII).
 * NULL or "" uses config.ini's opponent=. 0 on success. */
int slippi_find_match(const char *opponent_code);
/* Services the network on the caller's thread (cheap; call once per tick). */
void slippi_poll(void);
int slippi_status(void);
/* Copies the last error/status text; returns its length. */
int slippi_error(char *buf, int len);
/* Leaves the match / cancels the search (graceful ENet disconnect). */
void slippi_disconnect(void);

/* Match information (valid once CONNECTING/CONNECTED). */
int slippi_is_decider(void);          /* MM isHost; the decider is player index 0 in 1v1 */
int slippi_local_index(void);         /* our player index (MM port - 1), -1 if none */
enum { SLIPPI_INFO_NAME = 0, SLIPPI_INFO_CODE = 1, SLIPPI_INFO_UID = 2, SLIPPI_INFO_MATCH_ID = 3, SLIPPI_INFO_LOCAL_NAME = 4, SLIPPI_INFO_LOCAL_CODE = 5 };
int slippi_info(int what, char *buf, int len);  /* remote name/code are UTF-8 from the MM server */
/* MM "stages" (allowed stage ids, one byte each); returns the count. */
int slippi_mm_stages(unsigned char *out, int max);

/* Local selections (Dolphin SetMatchSelections). character < 0: character not
 * selected; stage < 0: stage not selected. flags bits 0-7 team_id, bits 8-15
 * alt_stage_mode (frozen Stadium). A fresh rng_offset (rand % 0xFFFF) is drawn
 * on every call, like Dolphin. The merged selections are sent reliably. */
void slippi_set_selections(int character, int color, int stage, int flags);
/* Selections as 13 bytes, exactly the body of a 0x82 packet:
 *  [0] character_id [1] color [2] is_character_selected [3] player_idx
 *  [4..5] stage_id (BE) [6] is_stage_selected [7..10] rng_offset (BE)
 *  [11] team_id [12] alt_stage_mode
 * slippi_remote_selections returns how many 0x82 packets arrived (0 = none). */
int slippi_remote_selections(unsigned char out[13]);
int slippi_local_selections(unsigned char out[13]);
/* The game's RNG offset: the decider's rng_offset (Dolphin prepareOnlineMatchState). */
unsigned slippi_rng_offset(void);

/* Call at frame 1 of every game, before the first slippi_send_pad (Dolphin
 * StartSlippiGame: clears the local pad queue, acks, timing and selections). */
void slippi_start_game(void);
/* Queues our input for tagged frame (local frame + local delay) and sends every
 * un-acked pad. The first pad of a game also queues zero pads for frames
 * 1..frame-1 (Dolphin's delay padding). pad == NULL or an old frame only
 * re-sends. checksum_frame/checksum go into the packet header (0/0 = none).
 * Returns 0, or -1 when not connected. */
int slippi_send_pad(int frame, const unsigned char *pad, int checksum_frame, unsigned checksum);
/* Remote input for frame: 1 copied to out[8], 0 not received yet, -1 evicted or invalid. */
int slippi_remote_pad(int frame, unsigned char *out);
/* Newest remote frame received this game (0 = none). */
int slippi_latest_remote_frame(void);
/* Latest remote checksum pair from the pad header. */
int slippi_remote_checksum_frame(void);
unsigned slippi_remote_checksum(void);
/* Dolphin CalcTimeOffsetUs: positive = we are ahead of the opponent. */
int slippi_time_offset_us(void);
/* Last pad round trip (pad sent -> ack), microseconds. */
int slippi_ping_us(void);
/* Chat: returns a received premade-chat id once (0 = none). */
int slippi_chat_poll(void);
void slippi_send_chat(int message_id);

/* Counters for logs, see enum below; returns the value. */
enum {
    SLIPPI_STAT_PAD_PACKETS_SENT, SLIPPI_STAT_PAD_PACKETS_RECEIVED, SLIPPI_STAT_PAD_PACKETS_STALE,
    SLIPPI_STAT_PAD_GAPS, SLIPPI_STAT_ACKS_SENT, SLIPPI_STAT_ACKS_RECEIVED, SLIPPI_STAT_RESENDS,
    SLIPPI_STAT_ENET_RTT_MS, SLIPPI_STAT_ENET_LOSS, SLIPPI_STAT_PEERS, SLIPPI_STAT_LAST_RECEIVE_MS,
    SLIPPI_STAT_LOCAL_QUEUE, SLIPPI_STAT_DROPPED_TEST, SLIPPI_STAT_COUNT
};
int slippi_stat(int which);

/* Boot self-test (config.ini selftest=1): see slippi_selftest.c. */
int slippi_selftest_requested(void);
int slippi_selftest_run(void);

#ifdef __cplusplus
}
#endif
#endif
