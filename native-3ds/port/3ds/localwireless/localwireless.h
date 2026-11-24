/* Local wireless lockstep for up to 4 consoles (3DS UDS), independent of any game.
 *
 * A lobby with a star: the host owns the session and up to three other consoles join and leave at any time. Each
 * console sits on a port (0..3; the host is 0, a newcomer takes the lowest free one). Before and between games the
 * host publishes an opaque lobby blob; consoles send it commands. When the host starts a game, every console that is
 * present submits its input for frame F to the host; the host waits until it holds all of them and sends each client
 * one *bundle* for F (every port's input plus a CPU mask). Every console, the host included, then runs frame F from
 * the bundle, so all consoles see the same inputs. The host alone decides that a console has dropped (it left the
 * network, or was silent for the timeout); it flips that port's bit in the CPU mask *between* frames, so the hand-over
 * happens at the same frame everywhere and the game only has to turn that player into an AI from then on.
 *
 * What the game supplies (a "syncing provider" in the game's own code):
 *   - the per-frame input bytes (any fixed size 1..LW_MAX_INPUT),
 *   - the lobby blob (host), commands (clients) and one "start" blob per game (the agreed match description),
 *   - an optional desync checksum with each input.
 * Nothing here knows about the game. All functions must be called with the caller's own lock held (one thread at a
 * time); lw_poll is the only one that needs to run regularly (every few ms), also while the game waits or loads.
 *
 * The platform shim (lw_platform.h) is the only thing the host project provides.
 * Transport: UDS on the 3DS; with use_udp, a loopback UDP stand-in for PC tools and for emulator tests (Azahar cannot
 * join a UDS room headless). Builds that define LW_NO_UDP leave the stand-in out. Datagrams may be lost or reordered;
 * the session re-sends whatever must arrive. */
#ifndef LOCALWIRELESS_H
#define LOCALWIRELESS_H
#include <stddef.h>
#include <stdint.h>

#define LW_MAX_PLAYERS 4
#define LW_MAX_INPUT 16        /* bytes of input per player per frame */
#define LW_MAX_START 400
#define LW_NAME_MAX 12
#define LW_MAX_LOBBY 200       /* bytes of the host's lobby blob (it is also the beacon, so at most LW_MAX_BEACON) */
#define LW_MAX_COMMAND 24      /* bytes of one console->host command */
#define LW_MAX_BEACON 200      /* bytes of the host's beacon blob (what a console browsing for games sees) */

enum { LW_IDLE = 0, LW_SEARCHING, LW_CONNECTED, LW_FAILED, LW_DISCONNECTED };

enum { LW_STAT_PACKETS_SENT, LW_STAT_PACKETS_RECEIVED, LW_STAT_PACKETS_STALE, LW_STAT_GAPS, LW_STAT_ACKS_SENT,
       LW_STAT_ACKS_RECEIVED, LW_STAT_RESENDS, LW_STAT_DROPPED_TEST, LW_STAT_MENU_SENT, LW_STAT_COUNT };

typedef struct {
    int host;              /* 1: create the lobby; 0: browse for lobbies nearby and join one with lw_join */
    int input_size;        /* bytes per player input per frame, 1..LW_MAX_INPUT (the same on every console) */
    int frame_us;          /* nominal frame time, paces resends (0 = 16683) */
    int timeout_ms;        /* silence after which a console has dropped (0 = 2000) */
    int use_udp, port;     /* loopback UDP link instead of UDS (PC tools, development builds); port: the base port */
    int test_drop_pct;     /* tests: lose this % of outgoing packets */
} lw_config;

/* 0 on success; on failure err holds the text. */
int lw_open(const lw_config *cfg, char *err, int errlen);
void lw_close(void);
/* Services the link and the session. Returns the status. */
int lw_poll(void);
int lw_status(void);
const char *lw_error(void);           /* why the status is FAILED or DISCONNECTED */

/* Valid once CONNECTED. Port 0 is the host. A console keeps its port while it is connected ("console n is port n");
 * lw_players() is the highest present port + 1 and lw_present() says which of those are there. */
int lw_players(void);
int lw_my_index(void);
const char *lw_player_name(int idx);  /* the name (3DS system name) of the console on that port, "" if none */
const char *lw_local_name(void);      /* this console's name, "" before open */

/* ---- browsing for games (a client before it joins) ----
 * lw_poll keeps a list of the lobbies in range. It keeps its order (new games last; one that goes quiet leaves after a
 * few scans). Each game has a key that stays the same while it is listed, so a game picked from an older copy of the
 * list is still the one joined. lw_join(key) starts joining it (UDS: blocks for a moment); the status stays SEARCHING
 * until the lobby arrives, or turns FAILED ("Could not join the game") if it does not within a few seconds. */
typedef struct {
    uint32_t key;               /* which game (stable while it is listed) */
    char host[LW_NAME_MAX];     /* the host console's name */
    int players, max;           /* consoles in the lobby / most it takes */
    int len;
    uint8_t data[LW_MAX_BEACON];
} lw_net;
int lw_scan_count(void);
int lw_scan_get(int i, lw_net *out);    /* 0 ok, -1 no such game */
int lw_join(uint32_t key);              /* 0: joining; -1: failed (lw_error says why), still browsing */
int lw_browsing(void);                  /* 1 until lw_join succeeds */
void lw_host_set_beacon(const uint8_t *blob, int len);   /* host: what browsing consoles see of this lobby (<= LW_MAX_BEACON) */

/* ---- the lobby ----
 * Before and between games the host owns a small opaque blob (the game's lobby state) that is sent to every console,
 * together with who is present, their names and a 0..3 signal level (how recently the host heard each console).
 * Everyone sees the same snapshot (blob, present mask, names, signals) once it arrives; a snapshot is never mixed from
 * two versions. Consoles send the host commands (reliable, in order, one at a time). While a game runs, only consoles
 * that are not in it (late joiners) keep receiving the lobby, so the host can show them a status. */
unsigned lw_present(void);                        /* bit i: a console is on port i */
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

/* ---- menu input (outside games) ----
 * A few bytes per console of which only the newest value matters (e.g. a stick that turns a menu's camera). Lossy: a
 * value is never acknowledged; it goes out when it changes and is repeated a few times a second while it is not all
 * zero. A client sends its value to the host, the host sends every port's value to every client, so a console only
 * ever sees values that came through the host. A port's value reads as zero once nothing newer came for a moment (so a
 * lost "back to zero" cannot stick), when its console leaves, and while a game runs; this console's own value reads
 * as zero once it has not been set for a moment. Nothing goes out while this console is in a game. */
#define LW_MAX_MENU 4
void lw_menu_input_set(const uint8_t *data, int len);    /* this console's value (len <= LW_MAX_MENU, the rest zero) */
int lw_menu_input_get(int port, uint8_t *out);           /* out: LW_MAX_MENU bytes; 1 when port has a current value */

/* ---- starting a game ---- */
/* Host: start a game with this blob (e.g. the match description). Resets all per-game state, so no input from an
 * earlier game is used, and sends the blob to every console that is present until it has started. Consoles that join
 * later cannot take part in this game. 0 ok; -2 the last game's final bundles are still going out (try again). */
int lw_host_start(const uint8_t *blob, int len);
/* Host: the same, for a follow-up game with only the consoles that played the last game to its end (a console that
 * joined or dropped out meanwhile waits for the next one), e.g. a sudden death after a tie. */
int lw_host_start_again(const uint8_t *blob, int len);
/* Everyone: the current game's start blob (the host's included); its length, 0 if not here yet. */
int lw_get_start(uint8_t *out, int cap);
/* Call when frame 1 begins: the start blob is consumed. */
void lw_game_begin(void);

/* ---- during the game (frames count from 1) ---- */
/* Our input for `frame` (NULL = all zero). Frames must be submitted in order; submit `delay` frames ahead of
 * the frame being run, so the bundle has time to come back. The checksum pair is sent along. */
void lw_submit(int32_t frame, const uint8_t *input, int32_t checksum_frame, uint32_t checksum);
/* 1 when the bundle for `frame` is here: inputs[i] is port i's input (zero for a CPU, a dropped console or a port with
 * no console), *cpu_mask has bit i set when port i's input is not coming (no console in the game there, or it dropped:
 * from the frame on which it did). 0: not yet. */
int lw_bundle(int32_t frame, uint8_t inputs[LW_MAX_PLAYERS][LW_MAX_INPUT], uint8_t *cpu_mask);
/* The newest desync checksum received (host: from a client; client: from the host). Returns its frame, 0 if none. */
int32_t lw_remote_checksum(uint32_t *checksum);
int lw_ping_us(void);                 /* round trip to the host (clients), 0 if unknown */
unsigned lw_stat(int what);

#endif
