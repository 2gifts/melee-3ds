/* Melee -> Solo / Group, native side. The VS menu's Melee row asks the player whether to play Solo (vanilla) or
 * Group (a local wireless lobby); the Group page lists the games nearby and offers Host. The bottom screen draws the
 * view (bottom_draw.c) and sends touches here; the engine (mnmain.c's VS menu think) polls once per frame and enters
 * the vanilla flow or the lobby CSS. The session itself is group_session.c. */
#ifndef GROUP_MENU_H
#define GROUP_MENU_H
#include "group_lobby.h"
#include "group_session.h"

#define GROUP_ROWS 4

enum { GROUP_PAGE_NONE, GROUP_PAGE_ASK, GROUP_PAGE_LIST };

typedef struct {
    int page;                  /* GROUP_PAGE_* */
    int count;                 /* games found (the first GROUP_ROWS are shown) */
    group_game row[GROUP_ROWS];
    int busy;                  /* 1 while a Host or Join is under way */
    char error[96];
    unsigned serial;           /* changes whenever anything shown changes */
} GroupView;

/* Is the group menu on (group.ini menu=1)? */
int group_enabled(void);
/* Engine (through the bridge, group_bridge.h). */
int group_open(void);                        /* Melee was chosen: 1 = the Solo/Group page is up (wait), 0 = vanilla */
int group_menu(void);                        /* 1: choose on the top-screen submenu (group.ini top_menu=1; needs its artwork) */
int group_pick(int group);                   /* the top-screen menu's choice: 0 Solo, 1 Group */
int group_waiting(void);                     /* 1 while the page is up */
int group_poll(int back);                    /* once per frame while waiting; back = B pressed. 0 waiting, 1 Solo, 2 Group lobby, -1 cancelled */
int group_active(void);                      /* 1 while a group session opened from the menu is up (the CSS scene is the lobby) */
void group_leave(void);                      /* backed out of the lobby CSS: close the session */
int group_returning(void);                   /* 1 once after leaving: the menu shows the VS menu (reads and clears) */
/* Back on the lobby CSS: 1 = leave now. A host with other consoles here is asked first ("End the session?", drawn on the
 * bottom screen): 0 while the dialog is up, and the answer comes from group_confirm_state. */
int group_confirm_leave(void);
int group_confirm_state(void);               /* 0 waiting, 1 yes (then confirm_leave returns 1), 2 no */
int group_lost(void);                        /* 1 once the session ended under us (the host left, or the link failed) */
int group_fresh(void);                       /* 1 once on the VS menu after a lobby: clear its "entering a menu" state */
int group_reopen(void);                      /* 1 once, on the VS menu after that: open the Host/Join page again */
int group_toast_port(void);                  /* the port of the console the toast is about (its colour) */
int group_toast(char *out, int len);         /* the join/leave toast's text; returns the ms it has left, 0 when none */
int group_dialog(void);                      /* 1 while the dialog is up */
void group_dialog_answer(int yes);           /* touch */

/* While grouped (the lobby CSS, the stage select, the match): who is connected, for the bottom-screen strip. The strip
 * and the toasts are only used from the bottom screen's thread. */
typedef struct {
    int active;                /* a group session with a lobby */
    int me;                    /* this console's port */
    int phase;                 /* LOBBY_CSS / STAGE / MATCH */
    int watching;              /* a match runs that this console is not in (it joined during it): the Match in progress page */
    lobby_status status;       /* that match's timer, stocks and damage (status.valid once the host has sent it) */
    lobby_rules rules;
    struct {
        int present;           /* a console is on this port */
        int signal;            /* 0..3 bars */
        char name[32];         /* its 3DS system name (never its name tag) */
        int busy_why;          /* what it is busy with (LOBBY_BUSY_*: a slider, its name tag list, the rules, name entry) */
        lobby_slot door;       /* the port's door: Human, CPU or empty, and the fighter */
    } port[LOBBY_SLOTS];
    char toast[48];            /* "NAME JOINED" / "NAME LEFT" for a few seconds after a console came or went, else "" */
} GroupStrip;
int group_strip(GroupStrip *out);
unsigned group_serial(void);                 /* changes whenever the Group pages or the strip show something new */

/* Bottom screen (touch). */
const GroupView *group_view(void);
void group_pick_solo(void);
void group_pick_group(void);
void group_host(void);
void group_join_row(int row);                /* a full game (4 consoles) is refused with an error on the page */
void group_back(void);
void group_note_touch(unsigned x, unsigned y);   /* log where the bottom screen was tapped on a group page */

/* HOME and sleep: off while a wireless session is open (browsing, the lobby, a match, its results), on otherwise.
 * Call once per frame (the engine's frame and its lockstep waits); logs "group: HOME and sleep off/on" on a change. */
void group_system_frame(void);
int group_in_wireless(void);                 /* 1 while a session is open from the menu and not failed or ended */
int group_home_note(void);                   /* 1 for 2 s after a refused HOME press: the bottom screen shows a notice */

#endif
