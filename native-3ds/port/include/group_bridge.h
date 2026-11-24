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
    unsigned char spare;
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
} MpGroupRules;

/* The lobby as this console sees it (the host's newest snapshot). */
typedef struct MpGroupLobby {
    unsigned char valid;           /* 1 once there is a lobby */
    unsigned char me;              /* this console's port */
    unsigned char host;            /* 1: this console hosts */
    unsigned char present;         /* bit n: a console is on port n */
    unsigned char phase;           /* 0 character select, 1 stage select, 2 match */
    unsigned char starter;         /* the port that pressed Start */
    unsigned char stage_picked;
    unsigned char mode;            /* 0 normal, 1 sudden death */
    unsigned char stage_hi, stage_lo, alt_stage;
    unsigned char watching;        /* 1: a match runs that this console is not in (it joined during it); its CSS is
                                    * read-only until that match ends */
    unsigned char busy;            /* bit n: the console on port n is in its name tag list or name entry (READY waits) */
    unsigned char signal[4];       /* 0..3 bars per port */
    MpGroupRules rules;
    MpGroupDoor door[4];
} MpGroupLobby;

/* Melee -> Solo / Group (the VS menu) and the session around the lobby (port/3ds/group/group_menu.c). */
int mp_platform_group_open(void);          /* Melee chosen: 1 = the Solo/Group page is up (wait), 0 = vanilla */
int mp_platform_group_menu(void);          /* 1: the top-screen submenu is on (group.ini top_menu=1) */
int mp_platform_group_pick(int group);     /* the top-screen menu's choice: 0 Solo, 1 Group */
int mp_platform_group_waiting(void);       /* 1 while that page is up */
int mp_platform_group_poll(int back);      /* each frame while waiting: 0 wait, 1 Solo, 2 lobby, -1 cancelled */
int mp_platform_group_active(void);        /* 1 while a group session is open */
void mp_platform_group_leave(void);        /* backed out of the lobby CSS */
int mp_platform_group_returning(void);     /* 1 once after leaving: show the VS menu */
int mp_platform_group_confirm_leave(void); /* Back on the lobby CSS: 1 = leave now, 0 = "End the session?" is up */
int mp_platform_group_confirm_state(void); /* while it is: 0 waiting, 1 yes, 2 no */
int mp_platform_group_lost(void);          /* 1 once the session ended under us (the host left or the link failed) */
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
void mp_platform_group_lobby_busy(int busy);        /* this console's name tag list or name entry is open (1) or not */
void mp_platform_group_lobby_rules(const MpGroupRules *rules);

/* The match. */
int mp_platform_group_players(void);       /* 0 outside a group session, else the highest occupied port + 1 */
int mp_platform_group_player_name(int port, char *buf, int len);   /* the console on that port's 3DS name, "" if none */
void mp_platform_group_game_end(void);     /* the match is over on this console: back to the lobby */
/* The match that just ended was tied (every console sees the same result): the host starts a sudden death game with the
 * same consoles (block trailer LOBBY_BLOCK_SUDDEN set; each console sets it up from its own copy of the result). Other
 * consoles do nothing here; they wait for that start. */
void mp_platform_group_sudden_death(void);

#endif
