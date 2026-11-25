/* Group play's network side: one local wireless session (port/3ds/localwireless) carrying a Melee lobby
 * (group_lobby.h) and lockstep matches. It is the backend behind the engine's online bridge while a group session is
 * open (port/3ds/net_select.c), the lobby the character select mirrors (group_bridge.h), and what the Solo / Group
 * pages drive (group_menu.c).
 *
 * Every function here takes the platform lock itself (sp_lock, not recursive): call none of them with it held.
 * A worker thread services the link every couple of milliseconds; without one, group_net_poll does.
 *
 * Settings come from group.ini in the data folder (sp_data_dir), all optional:
 *   menu=1         Melee on the VS menu asks Solo / Group (0: vanilla VS). Default 1; 0 in development builds
 *   top_menu=1     ... on the top-screen submenu (default; its art is built from the menu's own letters, and without
 *                  vanilla's English menu archive the bottom-screen page is used); 0: the bottom-screen page
 *   delay=2        input delay of group matches, 1..15 frames
 *   timeout_ms=2000  silence after which a console has dropped
 *   udp=1, port=N  development builds and PC tools: the loopback link instead of UDS (base port N)
 *   drop_pct=N     tests: lose N % of outgoing packets
 *   net_thread=1, net_thread_core=-2   the worker thread
 *   auto_players=2 automatic sessions (opponent=@host / @join in the engine's config, tests): the host starts a match by
 *                  itself once this many consoles are here and every one has picked
 *   test_wireless_off=1  tests: every session fails to open with "Please enable wireless and try again"
 *   test_scan_locked=1   tests: the scan for games runs with the session lock held, so the engine waits for it
 *   test_vs_matches=N    tests (development builds): as a session's first lobby opens, this save's VS match counters
 *                  (the ones the stage unlock and challenger checks read) are raised to N
 *   test_nametag=N       tests (development builds): this console's player has name tag id N - 1 in group matches
 *   name=N         this console's name (tests); without it the Mii name, then the system name, then "3DS"
 *   test_screen_name=N, test_screen_name_fail=1   tests: the Mii name's FRD call is skipped; N is its result, or it
 *                  fails (fail wins), so the chain falls to the system name
 *   test_mii_fail=1      tests: the host's Mii (FRD_GetMyMii) is skipped and treated as failed: no Mii in the beacon
 *   test_mii_corrupt=1   tests: the host flips one byte of its Mii after the CRC, so browsers must reject it
 *   test_mii=HEX         tests (PC hosts): this Mii (192 hex digits, CFLStoreData) instead of FRD's */
#ifndef GROUP_SESSION_H
#define GROUP_SESSION_H
#include "group_bridge.h"
#include "group_lobby.h"
#include <stdint.h>

typedef struct {
    int menu, top_menu;
    int delay, timeout_ms;
    int udp, port, drop_pct;
    int net_thread, net_thread_core;
    int auto_players;
    int test_wireless_off;   /* tests: opening a session fails as if wireless communication were off */
    int test_scan_locked;    /* tests: the network thread scans with the session lock held (the engine waits for it) */
    int test_vs_matches;     /* tests (development builds): raise this save's VS match counters to N (unlock checks) */
    int test_nametag;        /* tests (development builds): this console plays with name tag id N - 1 (0: off) */
    int test_screen_name_fail;   /* tests: the Mii name (FRD) is skipped and treated as failed */
    char test_screen_name[32];   /* tests: the Mii name (FRD) is skipped and this is its result */
    char name[32];        /* name=: this console's name (tests; UTF-8, up to 10 characters, LW_NAME_MAX) */
    int test_mii_fail, test_mii_corrupt;
    uint8_t test_mii[LOBBY_MII_BYTES];   /* test_mii=: a Mii for hosts without FRD (PC); test_mii_ok when it parsed */
    int test_mii_ok;
} group_config;

/* group.ini, read each time (missing keys keep their defaults). */
void group_config_load(group_config *cfg);
/* An integer key from an ini text ("key=value" lines), def when missing. */
int group_ini_int(const char *text, const char *key, int def);
/* A text key: the rest of its line, cut to fit `len` bytes (not inside a UTF-8 character); "" when missing. */
void group_ini_str(const char *text, const char *key, char *out, int len);

/* ---- the session ---- */
/* Open a lobby: host it, or browse the games nearby (join one with group_join). automatic: the test mode of
 * opponent=@host / @join (a client joins the first game found, the host starts by itself). 0 ok; on failure the status
 * is FAILED and group_net_error says why. */
int group_session_start(int host, int automatic);
void group_session_stop(void);
int group_session_open(void);                  /* a session is open (the online bridge goes to it) */
int group_my_mii(uint8_t *out);                /* this console's Mii (LOBBY_MII_BYTES), read as the session opened; 0 if none */
int group_is_host(void);
int group_my_port(void);

/* Browsing (a client before it joins). */
typedef struct {
    uint32_t key;              /* which game; join by this, the list may change under a held copy */
    char host[32];             /* the host's name (its Mii name, else its system name, else "3DS"; UTF-8) */
    int players, max;          /* consoles in the game / most it takes */
    lobby_beacon beacon;       /* the host's beacon: doors, in match or not */
    int has_mii;               /* the beacon carried the host's Mii and its CRC holds */
    uint16_t mii_crc;          /* that Mii's CRC (with key, what the icon cache is keyed by) */
    uint8_t mii[LOBBY_MII_BYTES];
} group_game;
int group_scan(group_game *out, int max);      /* the games in range, in list order; how many */
int group_join(uint32_t key);                  /* 0: joining; -1: failed (group_net_error) */

/* The lobby, natively (the bottom screen). */
void group_test_hold_view(int hold);           /* tests (PC): a joiner keeps its lobby view as it is while held,
                                                * as if no snapshot got through */
int group_view_since_game_end(void);           /* the lobby view arrived after this console's last match ended
                                                * (always on the host): a character select phase in it is after the
                                                * results, not from before the match */
int group_lobby_view(lobby_state *out);        /* 1 when there is a lobby; fills the host's newest state */
unsigned group_present(void);                  /* bit n: a console is on port n */
int group_watching(void);                      /* a match runs that this console is not in (it joined during it), or
                                                * that match's results screen is up */
/* Host: the running match's timer, stocks and damage, for consoles that joined during it. Call as often as convenient
 * (the bottom screen's thread does, every frame); it goes out a few times a second, only during a match. */
void group_match_status(const lobby_status *st);
int group_signal(int port);                    /* that console's link: 0..3 signal bars */
void group_name(int port, char *out, int len); /* that console's name (as host[] above), "" if none */

/* The lobby for the engine (group_bridge.h). */
int group_lobby_get(MpGroupLobby *out);
void group_lobby_set_door(int port, const MpGroupDoor *door);
void group_lobby_start(void);
void group_lobby_stage(int stage, int alt_stage);
void group_lobby_back(void);
void group_lobby_busy(int busy);
void group_lobby_rules(const MpGroupRules *rules);
void group_menu_sticks(int x, int y, signed char *out);
int group_door_names(void);                    /* 1: names under the character select doors (0 in builds made with
                                                * build_game.py --no-door-names) */
int group_players(void);
int group_player_name(int port, char *buf, int len);
void group_game_end(void);
void group_results_press(int ready, int advance);
void group_results_end(const unsigned char *stars, const unsigned char *handicap, int loser);
void group_sudden_death(void);
int group_test_value(int which);               /* 0 test_vs_matches, 1 test_nametag (development builds; 0 otherwise) */                 /* the match just ended tied: the host starts its sudden death */

/* The online bridge's calls while a group session is open (slippi_net_bridge.h, through net_select.c). */
int group_net_start(const char *role);         /* "@host" or "@join": an automatic session */
int group_net_status(void);
const char *group_net_error(void);
void group_net_poll(void);
void group_net_stop(void);
int group_net_local_index(void);
void group_net_set_selections(int char_id, int color, int stage_id, int stage_selected);
int group_net_remote_ready(void);
int group_net_match_block(unsigned char *out);
void group_net_new_game(void);
int group_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum, const unsigned char *local_pad12,
                          unsigned char *out);
int group_net_remote_checksum(int *unused);
int group_net_remote_checksum_frame(void);
unsigned group_net_remote_checksum_value(void);
int group_net_ping_ms(void);
int group_net_time_offset_us(void);
int group_net_stat(int which);                 /* LW_STAT_* (tests) */

#endif
