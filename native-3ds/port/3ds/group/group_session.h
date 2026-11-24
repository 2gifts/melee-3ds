/* Group play's network side: one local wireless session (port/3ds/localwireless) carrying a Melee lobby
 * (group_lobby.h) and lockstep matches. It is the backend behind the engine's online bridge while a group session is
 * open (port/3ds/net_select.c), the lobby the character select mirrors (group_bridge.h), and what the Solo / Group
 * pages drive (group_menu.c).
 *
 * Every function here takes the platform lock itself (sp_lock, not recursive): call none of them with it held.
 * A worker thread services the link every couple of milliseconds; without one, group_net_poll does.
 *
 * Settings come from group.ini in the data folder (sp_data_dir), all optional:
 *   menu=1         Melee on the VS menu asks Solo / Group (off: vanilla VS)
 *   top_menu=1     ... on the top-screen submenu instead of the bottom-screen page (placeholder art)
 *   delay=2        input delay of group matches, 1..15 frames
 *   timeout_ms=2000  silence after which a console has dropped
 *   udp=1, port=N  development builds and PC tools: the loopback link instead of UDS (base port N)
 *   drop_pct=N     tests: lose N % of outgoing packets
 *   net_thread=1, net_thread_core=-2   the worker thread
 *   auto_players=2 automatic sessions (opponent=@host / @join in the engine's config, tests): the host starts a match by
 *                  itself once this many consoles are here and every one has picked */
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
} group_config;

/* group.ini, read each time (missing keys keep their defaults). */
void group_config_load(group_config *cfg);
/* An integer key from an ini text ("key=value" lines), def when missing. */
int group_ini_int(const char *text, const char *key, int def);

/* ---- the session ---- */
/* Open a lobby: host it, or browse the games nearby (join one with group_join). automatic: the test mode of
 * opponent=@host / @join (a client joins the first game found, the host starts by itself). 0 ok; on failure the status
 * is FAILED and group_net_error says why. */
int group_session_start(int host, int automatic);
void group_session_stop(void);
int group_session_open(void);                  /* a session is open (the online bridge goes to it) */
int group_is_host(void);
int group_my_port(void);

/* Browsing (a client before it joins). */
typedef struct {
    uint32_t key;              /* which game; join by this, the list may change under a held copy */
    char host[32];             /* the host's 3DS name */
    int players, max;          /* consoles in the game / most it takes */
    lobby_state lobby;         /* the host's beacon: doors, stage, in match or not */
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
int group_signal(int port);                    /* 0..3 */
void group_name(int port, char *out, int len); /* that console's 3DS name, "" if none */

/* The lobby for the engine (group_bridge.h). */
int group_lobby_get(MpGroupLobby *out);
void group_lobby_set_door(int port, const MpGroupDoor *door);
void group_lobby_start(void);
void group_lobby_stage(int stage, int alt_stage);
void group_lobby_back(void);
void group_lobby_busy(int busy);
void group_lobby_rules(const MpGroupRules *rules);
void group_menu_sticks(int x, int y, signed char *out);
int group_door_names(void);
int group_players(void);
int group_player_name(int port, char *buf, int len);
void group_game_end(void);
void group_results_press(int ready, int advance);
void group_results_end(const unsigned char *stars, const unsigned char *handicap);
void group_sudden_death(void);                 /* the match just ended tied: the host starts its sudden death */

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
