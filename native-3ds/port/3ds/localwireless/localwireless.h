/* Local wireless lockstep for 2 to 4 consoles (3DS UDS), independent of any game.
 *
 * A star: the host (node 1) owns the session. Every console submits its input for
 * frame F to the host; the host waits until it holds all of them and sends each
 * client one *bundle* for F (every player's input plus a CPU mask). Every console,
 * the host included, then runs frame F from the bundle, so all consoles see the
 * same inputs. The host alone decides that a console has dropped (it left the
 * network, or was silent for the timeout); it flips that player's bit in the CPU
 * mask *between* frames, so the hand-over happens at the same frame everywhere
 * and the game only has to turn that player into an AI from then on.
 *
 * What the game supplies (a "syncing provider" in the game's own code):
 *   - the per-frame input bytes (any fixed size 1..LW_MAX_INPUT),
 *   - a small "setup" blob per console before the match (character picks etc.)
 *     and one "start" blob from the host (the agreed match description),
 *   - an optional desync checksum with each input.
 * Nothing here knows about the game. All functions must be called with the
 * caller's own lock held (one thread at a time); lw_poll is the only one that
 * needs to run regularly (every few ms), also while the game waits or loads.
 *
 * The platform shim (lw_platform.h) is the only thing the host project provides.
 * Transport: UDS on the 3DS; with use_udp, a loopback UDP stand-in for PC tools
 * and for emulator tests (Azahar cannot join a UDS room headless). Datagrams may be
 * lost or reordered; the session re-sends whatever must arrive. */
#ifndef LOCALWIRELESS_H
#define LOCALWIRELESS_H
#include <stddef.h>
#include <stdint.h>

#define LW_MAX_PLAYERS 4
#define LW_MAX_INPUT 16        /* bytes of input per player per frame */
#define LW_MAX_SETUP 32
#define LW_MAX_START 400
#define LW_NAME_MAX 12
#define LW_MAX_LOBBY 96        /* bytes of the host's lobby blob */
#define LW_MAX_COMMAND 24      /* bytes of one console->host command */
#define LW_MAX_BEACON 200      /* bytes of the host's beacon blob (what a console browsing for games sees) */
#define LW_MAX_NETS 8

enum { LW_IDLE = 0, LW_SEARCHING, LW_CONNECTED, LW_FAILED, LW_DISCONNECTED };

enum { LW_STAT_PACKETS_SENT, LW_STAT_PACKETS_RECEIVED, LW_STAT_PACKETS_STALE, LW_STAT_GAPS, LW_STAT_ACKS_SENT,
       LW_STAT_ACKS_RECEIVED, LW_STAT_RESENDS, LW_STAT_DROPPED_TEST, LW_STAT_COUNT };

typedef struct {
    int host;              /* 1: create the session and wait for `players` consoles; 0: find one and join */
    int players;           /* host: consoles in the match, 2..4 (joiners learn it from the host) */
    int input_size;        /* bytes per player input per frame, 1..LW_MAX_INPUT (the same on every console) */
    int frame_us;          /* nominal frame time, paces resends (0 = 16683) */
    int timeout_ms;        /* silence after which a console has dropped (0 = 2000) */
    int use_udp, port;     /* loopback UDP link instead of UDS (PC tools, development builds); port: host's */
    int test_drop_pct;     /* tests: lose this % of outgoing packets */
    int browse;            /* 1 (a lobby client): do not join by itself; list the games nearby (lw_scan_*) and join one with lw_join */
    int lobby;             /* 1: an open lobby (see "lobby" below); `players` is ignored, up to 4 consoles join and leave
                              at any time, and a host may be alone. 0: the fixed roster of `players` consoles. */
} lw_config;

/* 0 on success; on failure err holds the text. */
int lw_open(const lw_config *cfg, char *err, int errlen);
void lw_close(void);
/* Services the link and the session. Returns the status. */
int lw_poll(void);
int lw_status(void);
const char *lw_error(void);           /* why the status is FAILED or DISCONNECTED */

/* Valid once CONNECTED. Index 0 is the host. In a lobby, the index is the console's network node minus one and
 * stays the same while it is connected ("console n is port n"); lw_players() is then the highest present index + 1
 * and lw_present() says which of those are there. */
int lw_players(void);
int lw_my_index(void);
const char *lw_player_name(int idx);  /* the console's name (3DS system name), "" if unknown */
const char *lw_local_name(void);      /* this console's name, "" before open */

/* ---- browsing for games (cfg.browse, a lobby client) ----
 * lw_poll keeps a list of the lobbies in range. It keeps its order (new games last; one that goes quiet leaves after a few
 * scans). lw_join(i) starts joining that game (UDS: blocks for a moment); the status stays SEARCHING until the lobby
 * arrives, or turns FAILED ("Could not join the game") if it does not within a few seconds. */
typedef struct {
    char host[LW_NAME_MAX];     /* the host console's name */
    int players, max;           /* consoles in the lobby / most it takes */
    int len;
    uint8_t data[LW_MAX_BEACON];
} lw_net;
int lw_scan_count(void);
int lw_scan_get(int i, lw_net *out);    /* 0 ok, -1 no such game */
int lw_join(int i);                     /* 0: joining; -1: failed (lw_error says why), still browsing */
int lw_browsing(void);                  /* 1 until lw_join succeeds */
void lw_host_set_beacon(const uint8_t *blob, int len);   /* host: what browsing consoles see of this lobby (<= LW_MAX_BEACON) */

/* ---- the lobby (cfg.lobby) ----
 * Before and between games the host owns a small opaque blob (the game's lobby state) that is sent to every console,
 * together with who is present, their names and a 0..3 signal level (how recently the host heard each console).
 * Everyone sees the same snapshot (blob, present mask, names, signals) once it arrives; a snapshot is never mixed from
 * two versions. Consoles send the host commands (reliable, in order, one at a time). While a game runs, only consoles
 * that are not in it (late joiners) keep receiving the lobby, so the host can show them a status. */
unsigned lw_present(void);                        /* bit i: console i is here (all of them in a fixed-roster session) */
int lw_signal(int idx);                           /* 0..3, 3 = good; 0 if absent */
void lw_host_set_lobby(const uint8_t *blob, int len);    /* host: the lobby blob; sent when it changes */
int lw_get_lobby(uint8_t *out, int cap);          /* everyone: the newest blob (the host's own included); its length, 0 if none */
unsigned lw_lobby_version(void);                  /* changes whenever the snapshot (blob, present, names, signals) does */
int lw_match_running(void);                       /* a game is running (host) / the host says so (clients) */
int lw_in_match(void);                            /* this console takes part in the running game */
int lw_send_command(const uint8_t *cmd, int len); /* client: queue a command for the host; 0 ok, -1 if the queue is full */
int lw_poll_command(int *from, uint8_t *out, int cap);  /* host: the next command (in order per console); its length, 0 if none */
/* Both: the running game is over on this console. The host keeps re-sending the last bundles until every console has
 * them (or a few seconds pass), then everyone is back in the lobby. */
void lw_game_end(void);

/* ---- before the match ---- */
/* This console's setup blob. A client sends it to the host (re-sent until the start blob arrives); the host
 * keeps its own for index 0. Call again whenever it changes. */
void lw_set_setup(const uint8_t *blob, int len);
/* Host: console idx's latest setup blob (idx 0 = the host's own); its length, 0 if none yet. */
int lw_get_setup(int idx, uint8_t *out, int cap);
/* Host: start a game with this blob (e.g. the match description). Resets all per-game state, so no input
 * from an earlier game is used, and sends the blob to every console that is present until it has started.
 * (A lobby: consoles that join later cannot take part in this game.) */
int lw_host_start(const uint8_t *blob, int len);
/* Everyone: the current game's start blob (the host's included); its length, 0 if not here yet. */
int lw_get_start(uint8_t *out, int cap);
/* Call when frame 1 begins: the start blob is consumed and the setups are cleared for the next game. */
void lw_game_begin(void);

/* ---- during the match (frames count from 1) ---- */
/* Our input for `frame` (NULL = all zero). Frames must be submitted in order; submit `delay` frames ahead of
 * the frame being run, so the bundle has time to come back. The checksum pair is sent along. */
void lw_submit(int32_t frame, const uint8_t *input, int32_t checksum_frame, uint32_t checksum);
/* 1 when the bundle for `frame` is here: inputs[i] is player i's input (zero for a CPU or a dropped player),
 * *cpu_mask has bit i set from the frame on which player i is a CPU. 0: not yet. */
int lw_bundle(int32_t frame, uint8_t inputs[LW_MAX_PLAYERS][LW_MAX_INPUT], uint8_t *cpu_mask);
/* The newest desync checksum received (host: from a client; client: from the host). Returns its frame, 0 if none. */
int32_t lw_remote_checksum(uint32_t *checksum);
int lw_ping_us(void);                 /* round trip to the host (clients), 0 if unknown */
unsigned lw_stat(int what);

#endif
