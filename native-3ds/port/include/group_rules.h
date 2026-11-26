/* Group play's decisions as pure functions: the character select, the stage select and the results screen.
 *
 * The scenes' own code (the C that the tools/group_edits scripts splice into the decomp's mncharsel.c, mnstagesel.c and
 * gmresultplayer.c, and port/engine/group/group.c) copies the scene's state into the plain values and structures below,
 * calls one of these and applies the answer. The rules live in port/engine/group/css_rules.c, sss_rules.c and
 * results_rules.c. They read nothing else and change nothing outside what they are given, so the same files build into
 * the engine and into the host tests (tests/group_*_tests.c, tools/test_group.py). No decomp header is included here:
 * the scene's constants that a rule needs are copied below, and the glue checks them against the decomp's own. */
#ifndef GROUP_RULES_H
#define GROUP_RULES_H

#include <group_bridge.h>

typedef unsigned char GrByte;

/* ---- constants shared with the scenes (checked in the splices with _Static_assert) ---- */
#define GR_FIGHTERS 0x1A          /* CKind_Playable_Count: an external character id below this is a fighter */
#define GR_ICONS 0x19             /* the character select's fighter icons; sel_icon at or above this is none */
#define GR_NO_TAG 0x78            /* a door's name tag slot when it has none */
#define GR_PAD_A 0x0100           /* HSD_PAD_A, PAD_BUTTON_A */
#define GR_PAD_START 0x1000       /* PAD_BUTTON_START */
#define GR_TOKEN_REMOTE_HELD 5    /* a token (puck) held by another console (one held by a cursor here is port + 1) */
#define GR_DOOR_BYTES 15          /* a door as the character select compares it: kind, fighter, costume, team, level,
                                   * chosen, tag[8], handicap (MpGroupDoor's first 14 bytes and its handicap) */
#define GR_SSS_ICONS 0x1E         /* the stage select's icons; 0x1E is "nothing selected" */
#define GR_SSS_FROM_LOBBY 0x7FFF  /* a hidden pick whose stage is the lobby's (a random pick the host resolves) */

/* ================= the character select (css_rules.c) ================= */

/* What a door's cursor reads: GR_PAD_EMPTY (a pad with nothing pressed: a dialog is up), or the pad's index. */
#define GR_PAD_EMPTY (-1)
int gr_css_pad_for(int group, int dialog_up, int my, int port);
/* A door whose cursor is not this console's: never flipped to CPU, or given a random fighter, for lack of a pad. */
int gr_css_remote(int group, int my, int door);
/* The Human / CPU / closed click skips Human (unplugged) on this door: no console sits there (no lobby: no pad). */
int gr_css_unplugged(int group, int lob_ok, unsigned present, int pad_err, int door);
/* The rules banner and the Teams switch are the host's. */
int gr_css_can_edit_rules(int group, int lob_ok, int host);
/* A joiner's character select shows the host's rules (mode, time, stocks, stock time, handicap mode). */
int gr_css_shows_host_rules(int lob_ok, int host, int melee_valid);
/* While watching a match: the cursor moves, and its only action is A on the red Back button (top right). */
unsigned gr_css_trigger(int watching, int hand, float x, float y, unsigned trigger);
/* READY waits while another console is busy, and never shows while watching a match or under a dialog. */
int gr_css_hide_ready(int group, int dialog_up, int lob_ok, unsigned busy, int my, int watching);
/* READY waits while a door's token is held over a fighter (ours or one mirrored from another console). */
int gr_css_hovering(int group, int p_kind, int token);
/* Door d is in another console's hands: its Human / CPU / closed switch does nothing here. */
int gr_css_door_in_use(int group, int lob_ok, unsigned in_use, unsigned my_doors, int d);

/* Auto handicap: where a door's value comes from (the console whose save the match will use). */
enum { GR_HC_VANILLA, GR_HC_ZERO, GR_HC_SAVE, GR_HC_SHOWN };
int gr_css_auto_hc_from(int lob_ok, int port, int hc_mode, int kind, int my, int host);
/* A door's handicap as the lobby holds it: 0 when closed or handicap is off, else value when 1..9, else 0. value is the
 * auto handicap (mode 1) or the slider's (mode 2). kind: the lobby's (0 Human, 1 CPU, 2 closed). */
GrByte gr_css_door_hc(int kind, int hc_mode, int value);

/* A door as the scene holds it (mnCharSel_803F0DFC.doors[d], vs.start.players[d] and its token). */
typedef struct GrCssScene {
    int p_kind;          /* the scene's: 0 Human, 1 CPU, 3 closed */
    int sel_icon, sel_icon_prev;
    int token;           /* the door's token x5: 0 placed, port + 1 held by that cursor, GR_TOKEN_REMOTE_HELD */
    int ckind;           /* players[d].ckind */
    int icon_ckind;      /* the fighter of icon sel_icon (when sel_icon < GR_ICONS), else -1 */
    int teams;           /* vs.start.rules.is_teams */
    int color, costume, team, cpu_level;
    const char *tag;     /* the door's name tag text when gr_css_door_has_tag(); else NULL */
    int handicap;        /* gr_css_door_hc() for this door */
} GrCssScene;
/* The door carries its name tag's text to the lobby: this console's own Human door, with a tag in use. */
int gr_css_door_has_tag(int kind, int d, int my, int use_tag, int nametag);
/* The door as this console reports it. A token another console holds reports what was last applied (applied_ok). */
void gr_css_read_door(const GrCssScene *s, int my, int applied_ok, const GrByte applied[GR_DOOR_BYTES],
                      GrByte out[GR_DOOR_BYTES]);
/* The lobby's door as this console shows it. */
void gr_css_view_door(const MpGroupDoor *v, GrByte out[GR_DOOR_BYTES]);
/* The door sent to the host; spare is the door's kind before the edit plus 1 (0 when none is known yet). */
void gr_css_door_to_send(const GrByte now[GR_DOOR_BYTES], int spare, MpGroupDoor *out);
/* A shown door, applied to the scene. */
typedef struct GrCssPut {
    int kind;            /* the scene's p_kind: 0, 1 or 3 */
    int ckind;           /* the fighter to place, or -1 (none in the door's slot) */
    int costume, team, cpu_level;
    int fixed_hc;        /* the fixed handicap slider's new value, 0 to leave it */
    int remote_held;     /* the token is in another console's hand over nothing: no puck here */
    int preview;         /* the token is held over this fighter (chosen 2): show it see-through */
    int preview_fighter;
} GrCssPut;
void gr_css_put_plan(const GrByte v[GR_DOOR_BYTES], int hc_mode, GrCssPut *out);

/* The per-door sync between the scene and the lobby, once a frame (the scene at rest). */
typedef struct GrCssSync {
    GrByte applied[GR_DOOR_BYTES];    /* the door as last applied or sent */
    GrByte last_view[GR_DOOR_BYTES];  /* the lobby's door as last seen */
    GrByte last_view_ok;
    GrByte pending;                   /* frames a local edit is held before the lobby's answer counts */
} GrCssSync;
enum {
    GR_SYNC_SEND = 1,     /* send gr_css_door_to_send(now, ...) with the spare given in *spare */
    GR_SYNC_PUT = 2,      /* apply view to the scene (gr_css_put_plan) */
    GR_SYNC_REREAD = 4,   /* then read the door back into applied (gr_css_read_door) */
};
typedef struct GrCssSyncIn {
    int applied_ok;      /* every door was applied once since the scene began */
    int mine;            /* the door is this console's (its port) */
    int owned_elsewhere; /* another console sits on it and it is Human in the lobby */
    int watching;
    const GrByte *now;   /* gr_css_read_door() */
    const GrByte *view;  /* gr_css_view_door() */
} GrCssSyncIn;
/* What a sync did, for the scene's log (the first build of a scene logs nothing). */
enum { GR_SYNC_QUIET, GR_SYNC_RESTORED, GR_SYNC_SENT, GR_SYNC_APPLIED };
/* Returns GR_SYNC_* flags and updates the door's sync state (applied, last view, pending); *spare is the spare for a
 * send, *why a GR_SYNC_QUIET .. GR_SYNC_APPLIED for the log. */
int gr_css_sync_door(GrCssSync *s, const GrCssSyncIn *in, int *spare, int *why);
/* The host's handicap mode changed: a door that is not this console's own Human door reads differently now; take the
 * new reading as applied (no edit) and show the lobby's again. */
void gr_css_sync_hc_mode(GrCssSync *s, const GrByte now[GR_DOOR_BYTES]);

/* Busy: what this console is doing that the others must wait for, and the doors it is using. */
typedef struct GrCssBusy {
    int busy;            /* this console is busy (the lobby's busy bit) */
    int naming;          /* the door whose name entry this console opened, -1 none */
    int ruling;          /* this console opened the rules page */
    unsigned doors;      /* the doors in use here now */
    unsigned doors_sent; /* ... as last told to the host */
    int why_sent;
    int wait;            /* frames before the next send */
    int idle_resend;
} GrCssBusy;
typedef struct GrCssBusyIn {
    int scene;           /* the pending-scene byte: 0 at rest, 3 rules, 4 name entry (5 while either is up) */
    int name_door;       /* the door name entry was asked for */
    unsigned tag_lists;  /* doors whose name tag list is open */
    unsigned sliders;    /* doors with a CPU level or handicap slider held */
    int holding;         /* this console's cursor holds something */
    int dialog_up;
    int my;
    unsigned lob_busy, lob_in_use;
    int lob_why;         /* the lobby's busy reason for this console */
} GrCssBusyIn;
/* Returns 1 when the busy state must be sent: *send is the value for mp_platform_group_lobby_busy. */
int gr_css_busy_step(GrCssBusy *b, const GrCssBusyIn *in, int *send);
/* The scene began again: the doors and what was sent are forgotten (the rest stays, as before). */
void gr_css_busy_scene(GrCssBusy *b);

/* The stars over the doors follow the lobby's, except while the host's results screen is up (phase 3) or a sub-scene
 * is open. Returns the doors whose stars changed (to redraw). */
unsigned gr_css_stars(GrByte ko[4], const GrByte lobby[4], int phase, int scene);
/* Start on the character select: what to do. */
enum { GR_START_VANILLA, GR_START_ASK_HOST, GR_START_IGNORE };
int gr_css_start(int group, int go, int watching);
/* The host sent everyone to the stage select: seen the lobby's character select once, then another phase. */
int gr_css_go_sss(int seen_css, int go, int phase);
/* Back in a group: 1 when it waits for "End the session?" (or the host's answer) instead of leaving. */
int gr_css_back_waits(int group, int confirm_leave);
/* While Back waits for that answer: what the scene does (confirm_state: 0 waiting, 1 yes, 2 no). */
enum { GR_LEAVE_WAIT, GR_LEAVE_GO, GR_LEAVE_STAY };
int gr_css_leave_step(int confirm_state);
/* L+R+Start in the lobby is Back (unless a dialog is up or a sub-scene is pending). */
int gr_css_soft_reset_backs(int scene, int dialog_up);

/* The host's rules as the lobby carries them (all but melee[], which vanilla builds). */
typedef struct GrOwnRules {
    int mode, time_limit, stock_count, stock_time_limit, handicap, stage_sel, friendly_fire, teams;
    int item_freq;       /* 255 off */
} GrOwnRules;
void gr_css_rules_to_lobby(const GrOwnRules *r, MpGroupRules *out);
/* The joiner's banner key for the host's rules, and whether a change of it moves the sliders (a new handicap mode). */
int gr_css_rules_key(const MpGroupRules *r);
int gr_css_rules_relayout(int old_key, int new_key);

/* ================= the stage select (sss_rules.c) ================= */

/* The port that picks, as the scene keeps it (-1: any console). */
int gr_sss_picker(int lobby_picker);
/* Only the picker's console has a cursor and reads its pad. */
int gr_sss_no_cursor(int group, int picker, int me);
/* The Random icon on a console that is not the host: the host picks the stage. */
int gr_sss_random_to_host(int group, int lob_ok, int host);

/* One stage select frame's reaction to the lobby. */
typedef struct GrSssIn {
    int lost;            /* the session ended */
    int hidden;          /* a hidden pick's stage (GR_SSS_FROM_LOBBY: the lobby's), -1 none */
    int loading;         /* the pick state: 0 picking, 1 "Now loading", 2 done */
    int timer;           /* its timer */
    int remote, sent;    /* the pick came from another console / ours was sent */
    int resolving;       /* host: a random pick is being resolved here */
    int stage_picked, phase, host;
    int stage;           /* the lobby's stage, MP_GROUP_STAGE_RANDOM for Random */
} GrSssIn;
enum {
    GR_SSS_NONE,
    GR_SSS_LOST,         /* back to the character select, which leaves */
    GR_SSS_LEAVE,        /* a hidden pick is over: leave (stage: set it when set_stage) */
    GR_SSS_SEND,         /* our pick animates: tell the others */
    GR_SSS_RESOLVE,      /* host: another console pressed Random, pick from this save and send it */
    GR_SSS_HIDDEN,       /* load without showing the stage (stage: hidden's new value) */
    GR_SSS_SHOW,         /* someone picked this stage: find it (gr_sss_find) */
    GR_SSS_BACK,         /* the lobby is back on the character select */
};
typedef struct GrSssOut {
    int stage;
    int set_stage;
    int remote;          /* the pick is another console's from now on (the scene's remote flag) */
} GrSssOut;
int gr_sss_frame(const GrSssIn *in, GrSssOut *out);
/* A picked stage on this console's stage select: its icon, or GR_SSS_LOCKED (this save has not unlocked it), or
 * GR_SSS_UNKNOWN (no icon). stkind[i], unlocked[i]: icon i's stage and its unlock state (x8, 2 or more: shown). */
#define GR_SSS_LOCKED (-1)
#define GR_SSS_UNKNOWN (-2)
int gr_sss_find(const int stkind[GR_SSS_ICONS], const int unlocked[GR_SSS_ICONS], int stage);
/* Entering the stage select: the host runs vanilla's for Random and Ordered (its save picks at once). */
int gr_sss_host_picks(int host, int stage_sel);
/* The scene's picker field (unk_stage): the picker's port + 1, 0 when any console picks. */
int gr_sss_unk_stage(int lobby_picker);
/* Leaving with a stage: send it to the host unless it was sent already or came from another console. */
int gr_sss_exit_sends(int sent, int remote);

/* ================= the menus around the lobby (menu_rules.c) ================= */

/* Where the menu opens when the scene before it was the lobby (from_lobby) or Solo's character select (solo_back,
 * from the submenu): the submenu's row to hover (1 Group, 0 Solo), or -1 for vanilla's VS menu. */
int gr_menu_return_row(int from_lobby, int solo_back);
/* "Choose a game." after the Group list's poll this frame: on to the lobby (hosting, or joined), back to the menus
 * (B, the list's Back, or the network failed: the list is gone), or stay. */
enum { GR_CHOOSE_STAY, GR_CHOOSE_LOBBY, GR_CHOOSE_BACK };
int gr_choose_step(int poll, int listing);
/* Entering "Choose a game." from the lobby: the Group list is opened again unless it is up already or the menu reopens
 * it by itself (after a lost session). */
int gr_choose_opens_list(int listing, int reopened);

/* ================= the results screen (results_rules.c) ================= */

typedef struct GrResultsIn {
    int stage;           /* the screen's step: 0 a wait, 1 the intro, 2 waiting for a press, 3 the panels, 4 the end */
    int panels_live;     /* the panels take presses */
    unsigned shown_ready;/* the screen's ready bits */
    unsigned humans;     /* ports with a Human player in the match */
    int me;
    unsigned trigger;    /* this console's pad's new presses */
    int canceled;        /* the match was cancelled */
    int lob_ok;
    unsigned lob_busy;
    int since_end, phase;
    unsigned lob_ready;
    int lob_advance;
    int lost;
} GrResultsIn;
typedef struct GrResultsState {
    int early_advance;   /* a press made before the host's results screen was up, kept until it is */
    int busy_wait;
} GrResultsState;
typedef struct GrResultsOut {
    int send_busy;       /* mark this console busy (the host waits for its screen) */
    int presses;         /* how many of press[] to send, in order */
    struct { int ready, advance; } press[2];
    int kept;            /* a press was kept for later (log) */
    int withhold_mine;   /* before the panels none of this console's presses reach the screen (on them, its Start
                          * does not either: it goes to the host) */
    unsigned inject_start, inject_a;   /* ports that get Start / A this frame (the lobby's state as presses) */
    unsigned plug;       /* ports to show as plugged in (an injected press on a port with no Human) */
} GrResultsOut;
void gr_results_step(const GrResultsIn *in, GrResultsState *st, GrResultsOut *out);
/* The stars after a match: the lobby's (when known) plus each Human player's KOs, saturating at 255. */
void gr_results_stars(const GrByte lobby[4], int lobby_valid, const GrByte pkind[4], const int kos[4], GrByte out[4]);
/* Auto handicap after a match: the inputs (each door's handicap at the match start, 5 when not 1..9), then vanilla's
 * rule (fn_80169000, passed in), then 0 on empty doors; *write gets the doors whose new value this console's save
 * keeps: its own Human door and, on the host, the CPUs. Returns 0 (and leaves out zero) when there is nothing to do: the
 * match was not on auto handicap, or was cancelled. pkind: 0 Human, 1 CPU, 3 none. */
typedef void (*GrAutoHcRule)(void *match_end, GrByte hc[4]);
int gr_results_auto_hc(int was_auto, int canceled, const GrByte start_hc[4], const GrByte pkind[4], int me, int host,
                       GrAutoHcRule rule, void *match_end, GrByte out[4], unsigned *write);
/* The ports whose results window will show a console's name: those that are Human when the match begins (slot_type 0),
 * named after their console then. A player who drops later keeps the name; a CPU door shows its fighter's. A sudden
 * death keeps the tied match's names (it does not ask again). */
unsigned gr_results_name_ports(const GrByte slot_type[4]);

#endif
