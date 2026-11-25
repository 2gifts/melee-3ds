/* Engine-facing API of group play: Melee -> Group, local wireless lobbies of up to four 3DS consoles.
 *
 * The engine (big-endian) calls these through SDKBRIDGE entries in port/3ds/game_bridge.S; the native side is
 * port/3ds/group/ (names without the mp_platform_ prefix). During a group session the lockstep match itself runs
 * through the Slippi Direct bridge (slippi_net_bridge.h), which port/3ds/net_select.c hands to the group backend:
 * there, send_inputs answers with the host's bundle, 4 x 12 pad bytes (one per port, ours included; 8 wire bytes and 4
 * zero), [48] the CPU mask (bit n: port n's input is not coming, it is zero) and [49] the number of consoles, so the
 * output buffer needs 64 bytes.
 *
 * Structures crossing the bridge are made of single bytes only, so they have the same layout on both sides and byte
 * order never matters. */
#ifndef GROUP_BRIDGE_H
#define GROUP_BRIDGE_H

/* One door of the character select as the lobby holds it. */
typedef struct MpGroupDoor {
    unsigned char kind;            /* 0 Human, 1 CPU, 2 empty */
    unsigned char fighter;         /* external character id */
    unsigned char color;           /* costume */
    unsigned char team;            /* 0 red, 1 blue, 2 green */
    unsigned char level;           /* CPU level 1..9 */
    unsigned char chosen;          /* 0 no fighter, 1 placed, 2 held over a fighter (a preview), 3 held over nothing */
    unsigned char tag[8];          /* the name tag's game-encoded text, all zero = none */
    unsigned char handicap;        /* 1..9 under the host's handicap mode (0: off or not known yet); set by the console
                                    * that owns the door: a Human door's own console, a CPU's slider, the host for a CPU's
                                    * auto handicap */
    unsigned char spare;           /* sent: the door's kind before this edit, plus 1 (0: not known); see lobby_cmd_door */
} MpGroupDoor;

/* The host's rules, as its console sets them (the host sends them; other consoles' are ignored). */
typedef struct MpGroupRules {
    unsigned char time_mode;       /* 0 stock, 1 time */
    unsigned char stocks;
    unsigned char minutes;
    unsigned char items;           /* 0 off, else the item frequency + 1 */
    unsigned char teams;
    unsigned char friendly_fire;
    unsigned char melee_valid;     /* 1: melee[] holds the host's rules */
    unsigned char melee[0x38];     /* the host's StartMeleeRules (bytes 0..0x37, big-endian) as vanilla builds them for a VS
                                    * match from its rules menu; the match block takes its rules fields from it */
    unsigned char mode;            /* the host's GameRules: mode (0 time, 1 stock, 2 coin, 3 bonus) */
    unsigned char time_limit;      /* minutes (0 none) */
    unsigned char stock_count;
    unsigned char stock_time_limit;
    unsigned char handicap;        /* 0 off, 1 auto, 2 fixed */
    unsigned char stage_sel;       /* the host's Stage Selection rule: 0 any, 1 random, 2 ordered, 3 turns, 4 loser */
} MpGroupRules;

/* The lobby as this console sees it (the host's newest snapshot). */
typedef struct MpGroupLobby {
    unsigned char valid;           /* 1 once there is a lobby */
    unsigned char me;              /* this console's port */
    unsigned char host;            /* 1: this console hosts */
    unsigned char present;         /* bit n: a console is on port n */
    unsigned char phase;           /* 0 character select, 1 stage select, 2 match, 3 the match's results screen */
    unsigned char starter;         /* the port that pressed Start */
    unsigned char stage_picked;
    unsigned char mode;            /* 0 normal, 1 sudden death */
    unsigned char stage_hi, stage_lo, alt_stage;
    unsigned char watching;        /* 1: a match runs that this console is not in (it joined during it); its CSS is
                                    * read-only until that match ends */
    unsigned char busy;            /* bit n: the console on port n is busy: name tags, name entry, rules, a slider (READY waits) */
    unsigned char signal[4];       /* 0..3 bars per port */
    MpGroupRules rules;
    MpGroupDoor door[4];
    unsigned char played;          /* bit n: the console on port n played the match whose results are up (phase 3) */
    unsigned char ready;           /* bit n: port n's player is ready on the results screen (a console that left is) */
    unsigned char advance;         /* the results screen was moved on by a press: 1 past its wait, 2 past its intro */
    unsigned char stars[4];        /* the stars over each door (KOs per port, as vanilla's VS mode counts them; the host keeps them) */
    unsigned char since_end;       /* 1: this view arrived after this console's match ended (the host's: always), so a
                                    * phase 0 in it means the results are over, not a view from before the match */
    unsigned char in_use;          /* bit n: door n is in use on some console (a slider, its name tag list, name entry, a
                                    * held token): only that console may switch it between Human, CPU and closed */
    unsigned char busy_why[4];     /* why port n is busy: 1 a slider, 2 its name tag list, 3 the rules, 4 name entry,
                                    * 5 the prize screen after a match */
    unsigned char sss_picker;      /* phase 1: the port that picks the stage (0xFF: any console), by the host's Stage
                                    * Selection rule; Random and Ordered: the host, whose stage select picks at once */
} MpGroupLobby;
#define MP_GROUP_STAGE_RANDOM 0xFFFF   /* mp_platform_group_lobby_stage: the Random icon; the host picks from its save */

/* Melee -> Solo / Group (the VS menu) and the session around the lobby (port/3ds/group/group_menu.c). */
int mp_platform_group_open(void);          /* Melee chosen: 1 = the Solo/Group page is up (wait), 0 = vanilla */
int mp_platform_group_menu(void);          /* 1: the top-screen submenu is wanted (group.ini top_menu, on by default) */
int mp_platform_group_pick(int group);     /* the top-screen menu's choice: 0 Solo, 1 Group */
int mp_platform_group_waiting(void);       /* 1 while that page is up */
int mp_platform_group_listing(void);       /* 1 while the Group list (Host / Join) is up */
int mp_platform_group_poll(int back);      /* each frame while waiting: 0 wait, 1 Solo, 2 lobby, -1 cancelled */
int mp_platform_group_active(void);        /* 1 while a group session is open */
void mp_platform_group_leave(void);        /* backed out of the lobby CSS */
int mp_platform_group_returning(void);     /* 1 once after leaving: show the VS menu */
int mp_platform_group_confirm_leave(void); /* Back on the lobby CSS: 1 = leave now, 0 = "End the session?" is up */
int mp_platform_group_confirm_state(void); /* while it is: 0 waiting, 1 yes, 2 no */
void mp_platform_group_confirm_answer(int yes);   /* the top-screen "End the session?" was answered */
int mp_platform_group_lost(void);          /* 1 once the session ended under us (the host left or the link failed) */
int mp_platform_group_lost_note(char* out, int len);   /* VS menu: 1 with why the session ended, to show (read and clear) */
int mp_platform_group_fresh(void);         /* VS menu, once after a lobby: 1 = reset its entering-a-menu state */
int mp_platform_group_reopen(void);        /* VS menu, once after that: 1 = the Host/Join page is open again */
void mp_platform_group_notice(int kind);   /* a full bottom-screen notice while the engine blocks: 1 stage chosen, loading */
int mp_platform_group_toast(char *out, int len);   /* the join/leave toast's text; ms it has left, 0 when none */
int mp_platform_group_toast_port(void);    /* the port that toast is about (its colour) */

/* The lobby (port/3ds/group/group_session.c). The host owns the truth; any console may edit a door (another console's
 * Human door only as far as Human / CPU / closed and its team), press Start and pick the stage (the first pick wins);
 * only the host's rules are used. */
int mp_platform_group_lobby_get(MpGroupLobby *out);   /* 1 once a lobby exists */
void mp_platform_group_lobby_set_door(int port, const MpGroupDoor *door);
void mp_platform_group_lobby_start(void);
void mp_platform_group_lobby_stage(int stage, int alt_stage);
void mp_platform_group_lobby_back(void);
void mp_platform_group_lobby_back_reset(void);   /* L+R+Start on the stage select: the same, from any console */
/* This console is busy or not: bits 0..3 why (0 not busy, 1 a held slider or the results screen, 2 a name tag list, 3 the
 * rules menu, 4 name entry, 5 the prize screen); bits 4..7: the doors it is using (bit 4 + n: door n), see
 * MpGroupLobby.in_use. */
void mp_platform_group_lobby_busy(int busy);
void mp_platform_group_lobby_rules(const MpGroupRules *rules);
/* The C-stick that turns the character and stage selects' camera: this console's (x, y: -127..127, the stick times
 * 127) goes to every console; out[2n], out[2n+1] is port n's newest (this console's own included), 0 when that port has
 * none. Lossy and never waited for: a value nothing newer has replaced for a moment reads as 0. */
void mp_platform_group_menu_sticks(int x, int y, signed char *out);
/* 1 (the default): the character select draws each console's name under its door; 0 only in builds made with
 * --no-door-names (the bottom screen shows the names either way). */
int mp_platform_group_door_names(void);

/* The match. */
int mp_platform_group_players(void);       /* 0 outside a group session, else the highest occupied port + 1 */
#define MP_GROUP_NAME_MAX 32   /* a name: up to 10 characters of UTF-8 (30 bytes) and the NUL (LW_NAME_MAX) */
int mp_platform_group_player_name(int port, char *buf, int len);   /* the console on that port's 3DS name, "" if none */
void mp_platform_group_game_end(void);     /* the match is over on this console: its results screen follows */
/* The results screen (as vanilla's: each Human player's Start toggles their "ready", and it ends when all are). This
 * console's press: its port's ready and how far it moves the screen on (advance as in MpGroupLobby, 0 none); the host
 * applies it, every console follows the lobby. */
void mp_platform_group_results_press(int ready, int advance);
/* The results screen ended on this console. The host reopens the lobby with these stars (new totals per port) and, with
 * auto handicap, these door handicaps (1..9, 0 keeps the door's); loser: the match's loser as the host's engine decided
 * it (vanilla's VsModeData.winner, -1 none) for the Loser Stage Selection rule. Other consoles' calls change nothing. */
void mp_platform_group_results_end(const unsigned char *stars, const unsigned char *handicap, int loser);
/* The match that just ended was tied (every console sees the same result): the host starts a sudden death game with the
 * same consoles (block trailer LOBBY_BLOCK_SUDDEN set; each console sets it up from its own copy of the result). Other
 * consoles do nothing here; they wait for that start. */
void mp_platform_group_sudden_death(void);
/* Development builds, tests: a group.ini test key, 0 when not set and always in release builds. which 0:
 * test_vs_matches=N (as a session's first lobby opens, this save's VS match counters that its unlock checks read are
 * raised to N: a save one match short of a milestone); 1: test_nametag=N (this console's player plays with name tag
 * id N - 1 in group matches, as if chosen on the character select). */
int mp_platform_group_test_value(int which);

#endif
