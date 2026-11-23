/* Melee -> Solo / Group, native side The VS menu's Melee row asks the player whether to play
 * Solo (vanilla) or Group (a local wireless lobby); the Group page lists the games nearby and offers Host. The
 * bottom screen draws the view (bottom_draw.c) and sends touches here; the engine (mnmain.c's VS menu think) polls
 * once per frame and enters the vanilla flow or the lobby CSS. */
#ifndef SLIPPI_GROUP_H
#define SLIPPI_GROUP_H
#include "slippi_lobby.h"

#define GROUP_ROWS 4

enum { GROUP_PAGE_NONE, GROUP_PAGE_ASK, GROUP_PAGE_LIST };

typedef struct {
    char host[32];             /* the host's 3DS name */
    int players;               /* consoles in the game */
    lobby_state lobby;         /* the host's beacon: doors, stage, in match or not */
} SlippiGroupRow;

typedef struct {
    int page;                  /* GROUP_PAGE_* */
    int count;                 /* games found (the first GROUP_ROWS are shown) */
    SlippiGroupRow row[GROUP_ROWS];
    int busy;                  /* 1 while a Host or Join is under way */
    char error[96];
    unsigned serial;           /* changes whenever anything shown changes */
} SlippiGroupView;

/* Is the group menu on (config.ini local_group=1)? */
int slippi_group_enabled(void);
/* Engine (through the bridge). */
int slippi_group_open(void);                 /* Melee was chosen: 1 = the Solo/Group page is up (wait), 0 = vanilla */
int slippi_group_menu(void);                 /* 1: choose on the top-screen submenu (config local_group_menu=1; needs its artwork) */
int slippi_group_pick(int group);             /* the top-screen menu's choice: 0 Solo, 1 Group */
int slippi_group_waiting(void);              /* 1 while the page is up */
int slippi_group_poll(int back);             /* once per frame while waiting; back = B pressed. 0 waiting, 1 Solo, 2 Group lobby, -1 cancelled */
int slippi_group_active(void);               /* 1 while a group session is open (the CSS scene is the lobby) */
void slippi_group_leave(void);               /* backed out of the lobby CSS: close the session */
int slippi_group_returning(void);
/* Back on the lobby CSS: 1 = leave now. A host with other consoles here is asked first ("End the session?", drawn on the
 * bottom screen): 0 while the dialog is up, and the answer comes from slippi_group_confirm_state. */
int slippi_group_confirm_leave(void);
int slippi_group_confirm_state(void);        /* 0 waiting, 1 yes (then confirm_leave returns 1), 2 no */
int slippi_group_lost(void);                 /* 1 once the session ended under us (the host left, or the link failed) */
int slippi_group_fresh(void);                /* 1 once on the VS menu after a lobby: clear its "entering a menu" state */
int slippi_group_reopen(void);               /* 1 once, on the VS menu after that: open the Host/Join page again */
int slippi_group_toast_port(void);          /* the port of the console the toast is about (its colour) */
int slippi_group_toast(char *out, int len);  /* the join/leave toast's text; returns the ms it has left, 0 when none */
int slippi_group_dialog(void);               /* 1 while the dialog is up */
void slippi_group_dialog_answer(int yes);    /* touch            /* 1 once after leaving: the menu shows the VS menu (reads and clears) */

/* While grouped (the lobby CSS, the stage select, the match): who is connected, for the bottom-screen strip. */
typedef struct {
    int active;                /* a group session with a lobby */
    int me;                    /* this console's port */
    int phase;                 /* LOBBY_CSS / STAGE / MATCH */
    lobby_rules rules;
    struct {
        int present;           /* a console is on this port */
        int signal;            /* 0..3 bars */
        char name[32];         /* its 3DS name */
        lobby_slot door;       /* the port's door: Human, CPU or empty, and the fighter */
    } port[LOBBY_SLOTS];
    char toast[48];            /* "NAME JOINED" / "NAME LEFT" for a few seconds after a console came or went, else "" */
} SlippiGroupStrip;
int slippi_group_strip(SlippiGroupStrip *out);
unsigned slippi_group_serial(void);           /* changes whenever the Group pages or the strip show something new */

/* Bottom screen (touch). */
const SlippiGroupView *slippi_group_view(void);
void slippi_group_pick_solo(void);
void slippi_group_pick_group(void);
void slippi_group_host(void);
void slippi_group_join(int row);
void slippi_group_back(void);
void slippi_group_note_touch(unsigned x, unsigned y);   /* log where the bottom screen was tapped on a group page */

#endif
