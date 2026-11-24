/* The Melee lobby of a local wireless group: who sits in which of the four doors of the character select
 * screen, the host's rules summary and the stage pick. Pure data and rules, no I/O: group_session.c moves it through
 * the localwireless module (the host's lobby blob, consoles' commands) and turns it into the match block with
 * lobby_build_block. The host owns the state and applies commands in arrival order; everyone else shows the host's
 * snapshot.
 *
 * Four-GameCube-ports model: console n is port n; door n is port n's fighter and is Human, CPU or empty whether or
 * not a console sits on that port. */
#ifndef GROUP_LOBBY_H
#define GROUP_LOBBY_H
#include <stdint.h>

#define LOBBY_SLOTS 4
#define LOBBY_BLOB_MAX 200         /* LW_MAX_LOBBY */
#define LOBBY_TAG_BYTES 8          /* a name tag's game-encoded text (gm/types.h namedata) */
#define LOBBY_BLOCK_SIZE 0x140     /* match block + trailer (SLIPPI_NET_MATCH_BLOCK_TOTAL rounded for the 3 flag bytes) */

enum { LOBBY_HUMAN = 0, LOBBY_CPU = 1, LOBBY_EMPTY = 2 };
enum { LOBBY_CSS = 0, LOBBY_STAGE = 1, LOBBY_MATCH = 2, LOBBY_RESULTS = 3 };

typedef struct {
    uint8_t kind;                  /* LOBBY_HUMAN / LOBBY_CPU / LOBBY_EMPTY */
    uint8_t fighter;               /* external character id, meaningful when chosen */
    uint8_t color;                 /* costume */
    uint8_t team;                  /* 0 red, 1 blue, 2 green (used when the rules say Teams) */
    uint8_t level;                 /* CPU level 1..9 */
    uint8_t chosen;                /* 0 none, 1 a fighter is placed in the door, 2 the token is held over a fighter (preview), 3 held over nothing */
    uint8_t tag[LOBBY_TAG_BYTES];  /* name tag text, all zero = none */
    uint8_t handicap;              /* 1..9 under the host's handicap mode, 0 = off / not known (the template's) */
} lobby_slot;

#define LOBBY_MELEE_RULES 0x38     /* bytes of StartMeleeRules before its callbacks */

/* The host's rules: a summary for display everywhere, and the host's own StartMeleeRules as vanilla builds them for a
 * VS match from its rules menu (gm_80167BC8, run on the host), from which the match block takes the rules fields. */
typedef struct {
    uint8_t time_mode;             /* 0 stock, 1 time */
    uint8_t stocks;
    uint8_t minutes;
    uint8_t items;                 /* 0 off, else the item frequency */
    uint8_t teams;
    uint8_t friendly_fire;
    uint8_t melee_valid;           /* 1 once the host's console has sent melee[] */
    uint8_t melee[LOBBY_MELEE_RULES];   /* StartMeleeRules bytes 0..0x37, big-endian, as in the match block */
    /* The host's own GameRules fields, so a joiner's character select shows the host's banner and handicap sliders. */
    uint8_t mode;                  /* 0 time, 1 stock, 2 coin, 3 bonus */
    uint8_t time_limit, stock_count, stock_time_limit;
    uint8_t handicap;              /* 0 off, 1 auto, 2 fixed: the match block takes each door's handicap unless 0 */
} lobby_rules;

/* The running match as the host sees it, for consoles that are not in it (they joined during it): the timer and every
 * port's stocks and damage, refreshed a few times a second. Only meaningful while valid is 1 (phase MATCH). */
typedef struct {
    uint8_t valid;
    uint8_t timer;                 /* 1: the match shows a timer (seconds is its value), 0: none */
    uint16_t seconds;              /* the timer's value in seconds */
    struct {
        uint8_t kind;              /* the slot: 0 Human, 1 CPU, 3 none (Player_GetPlayerSlotType) */
        uint8_t stocks;
        uint16_t damage;           /* damage %, or HP in stamina */
    } port[LOBBY_SLOTS];
} lobby_status;

typedef struct {
    uint8_t phase;                 /* LOBBY_CSS / LOBBY_STAGE / LOBBY_MATCH / LOBBY_RESULTS */
    uint8_t starter;               /* console that pressed Start (phase STAGE) */
    lobby_rules rules;
    uint16_t stage;                /* the picked stage (phase STAGE once picked, and MATCH) */
    uint8_t stage_picked, alt_stage;
    uint8_t mode;                  /* 0 normal, 1 sudden death (second game of the same session) */
    uint8_t busy;                  /* bit n: the console on port n is busy (its name tag list, name entry, the host's
                                    * rules menu or a held slider); READY is hidden everywhere and Start is refused
                                    * while any bit is set */
    lobby_slot slot[LOBBY_SLOTS];
    lobby_status status;           /* the running match (host: lobby_host_set_status), at the end of the blob */
    /* The results screen after a match (phase RESULTS), the same on every console that played: vanilla's results end
     * once every Human player has pressed Start (each press toggles that player's "ready"). */
    uint8_t played;                /* bit n: the console on port n plays the running match (MATCH) or the one whose
                                    * results are up (RESULTS); a console not in it waits until the lobby reopens */
    uint8_t ready;                 /* bit n: port n's player is ready (RESULTS); a console that left counts as ready */
    uint8_t advance;               /* how far the results screen was moved on by a press: 1 past its wait (a cancelled
                                    * match), 2 past its intro (to the players' panels) */
    uint8_t stars[LOBBY_SLOTS];    /* the stars over each door: each port's KOs, as vanilla's VS mode counts them */
} lobby_state;

/* ---- the wire ---- */
int lobby_encode(const lobby_state *s, uint8_t *out, int cap);          /* bytes written, 0 if cap is too small */
int lobby_decode(lobby_state *s, const uint8_t *in, int len);           /* 0 ok, -1 not a lobby blob */

/* A console's commands to the host. Each returns its length. */
enum { LCMD_DOOR = 1, LCMD_START = 2, LCMD_STAGE = 3, LCMD_BACK = 4, LCMD_BUSY = 5, LCMD_RESULTS = 6 };
int lobby_cmd_door(uint8_t *out, int slot, const lobby_slot *s);       /* set door `slot` (any console may edit any door) */
int lobby_cmd_start(uint8_t *out);                                      /* Start from the CSS: go to the stage select */
int lobby_cmd_stage(uint8_t *out, int stage, int alt_stage);            /* a stage pick */
int lobby_cmd_back(uint8_t *out);                                       /* B at the stage select: back to the CSS */
int lobby_cmd_busy(uint8_t *out, int busy);                             /* the sender is busy (or no longer) */
int lobby_cmd_results(uint8_t *out, int ready, int advance);            /* results screen: the sender's ready, and how far a
                                                                         * press moved it on (0: no change) */

/* ---- the host ---- */
typedef struct {
    lobby_state st;
    unsigned present;              /* consoles the host last synced with */
    unsigned joined_in_match;      /* arrived during a match: their door is set when it ends */
    unsigned left_in_match;
    uint8_t last_tag[LOBBY_SLOTS][LOBBY_TAG_BYTES];   /* each port's last Human tag (a change clears its stars) */
    uint8_t last_tag_ok[LOBBY_SLOTS];
} lobby_host;

enum { LOBBY_CHANGED = 1, LOBBY_STAGE_REACHED = 2, LOBBY_START_MATCH = 4 };

void lobby_host_init(lobby_host *h, unsigned present);
/* Consoles joined or left (the module's present mask). A console leaving turns its Human door into a CPU level 3 (as a
 * controller unplugged on a GameCube), a console arriving on an empty port makes it Human with no fighter yet; a CPU
 * door stays a CPU. During a match this waits for lobby_host_match_over. Returns LOBBY_CHANGED if the state did. */
int lobby_host_sync(lobby_host *h, unsigned present);
/* One command from console `from` (never trust the contents: bad ones change nothing). Returns the LOBBY_* flags:
 * LOBBY_START_MATCH when the stage pick that ends the stage select arrived, so the caller builds the match block.
 * A door edit for a Human door that another console sits on changes only what any player may change there (Human,
 * CPU or closed, and the team); its fighter, costume and tag belong to the console on that port. */
int lobby_host_command(lobby_host *h, int from, const uint8_t *cmd, int len);
int lobby_host_set_door(lobby_host *h, int slot, const lobby_slot *s);       /* the host's own console edits */
int lobby_host_start(lobby_host *h, int from);                               /* the host's own Start press */
int lobby_host_pick_stage(lobby_host *h, int stage, int alt_stage);          /* the host's own stage pick */
int lobby_host_set_rules(lobby_host *h, const lobby_rules *r);      /* the host changed the rules */
int lobby_host_set_busy(lobby_host *h, int port, int busy);                  /* a console's busy bit (its own only) */
/* Phase MATCH; call after lw_host_start. `again`: a follow-up game (sudden death) with the consoles of the last one. */
void lobby_host_match_began(lobby_host *h, int again);
int lobby_host_set_status(lobby_host *h, const lobby_status *st);            /* the running match's status; LOBBY_CHANGED if it differs */
int lobby_host_match_over(lobby_host *h);                                    /* phase CSS again; applies waiting joins/leaves */
/* The match is over and its results screen is up: phase RESULTS until lobby_host_results_over. Joins and leaves still
 * wait; a console that left counts as ready. */
int lobby_host_results_begin(lobby_host *h);
/* Console `from`'s press on the results screen (only a console that played): its ready bit, and the screen moved on to
 * `advance` (0..2; it never goes back). */
int lobby_host_results(lobby_host *h, int from, int ready, int advance);
/* The results screen ended: the stars (new totals per port) and, with auto handicap, each door's new handicap (0: keep)
 * go into the lobby, then as lobby_host_match_over. */
int lobby_host_results_over(lobby_host *h, const uint8_t *stars, const uint8_t *handicap);

/* Can Start take the lobby to the stage select? at least two doors in use and a fighter in every one, with Teams at
 * least two different teams (as vanilla's READY overlay asks), and no console busy (lobby_state.busy). */
int lobby_can_start(const lobby_state *s);

/* ---- the match ---- */
/* Fills out[0..0x13F]: the game info block (Slippi's online template, the host's rules and a door per player), and the
 * trailer: rng_offset, local index (the caller overwrites it per console), delay, alt stage mode, and a zero byte that
 * marks a sudden death game when set (LOBBY_BLOCK_SUDDEN). From the host's StartMeleeRules it takes what the VS rules
 * menu decides: match kind and timer, friendly fire, pause, score display, item frequency and switch, the
 * self-destruct penalty, the time limit and the damage ratio; the rest of the template stays. Without them (the host's
 * console has not sent them yet) a stock match from the summary. */
void lobby_build_block(unsigned char *out, const lobby_state *s, unsigned rng_offset, int delay);
#define LOBBY_BLOCK_SUDDEN 0x13F   /* trailer byte: 1 = a sudden death game after a tie (every console sets it up from its
                                    * own copy of the last match's result) */

#endif
