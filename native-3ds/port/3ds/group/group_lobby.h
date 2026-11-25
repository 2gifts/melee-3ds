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
#define LOBBY_BLOB_MAX 255         /* LW_MAX_LOBBY */
#define LOBBY_NAME_BYTES 32        /* a console's name: up to 10 characters of UTF-8 and the NUL (LW_NAME_MAX) */
#define LOBBY_TAG_BYTES 8          /* a name tag's game-encoded text (gm/types.h namedata) */
#define LOBBY_BLOCK_SIZE 0x140     /* match block + trailer (SLIPPI_NET_MATCH_BLOCK_TOTAL rounded for the 3 flag bytes) */

enum { LOBBY_HUMAN = 0, LOBBY_CPU = 1, LOBBY_EMPTY = 2 };
/* Why a console is busy (its LCMD_BUSY code; the strip says what it is doing) */
enum { LOBBY_BUSY_NONE = 0, LOBBY_BUSY_SLIDER = 1, LOBBY_BUSY_TAGS = 2, LOBBY_BUSY_RULES = 3, LOBBY_BUSY_NAMING = 4,
       LOBBY_BUSY_PRIZE = 5,       /* the prize screen after a match (a notice or trophy this console's save earned) */
       LOBBY_BUSY_MAX = LOBBY_BUSY_PRIZE };
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
    uint8_t stage_sel;             /* the host's Stage Selection rule (LOBBY_SSS_*): who picks the stage, or which */
} lobby_rules;

/* The Stage Selection rule (vanilla's GameRules.stage_sel, Additional Rules): the host's decides for everyone. */
enum { LOBBY_SSS_ANY = 0, LOBBY_SSS_RANDOM = 1, LOBBY_SSS_ORDERED = 2, LOBBY_SSS_TURNS = 3, LOBBY_SSS_LOSER = 4 };
#define LOBBY_PICK_ANY 0xFF        /* lobby_state.sss_picker: every console may pick */
#define LOBBY_STAGE_RANDOM 0xFFFF  /* a pick of the Random icon from a console that is not the host: the host picks the
                                    * stage from its save (its Random Stage Switch, unlocks and no-repeat history) */

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
        uint8_t fighter, costume;  /* the fighter the match shows now (external id: Zelda 18 becomes Sheik 19 while
                                    * transformed) and its costume, for the bottom screens' stock icons */
    } port[LOBBY_SLOTS];
} lobby_status;

typedef struct {
    uint8_t phase;                 /* LOBBY_CSS / LOBBY_STAGE / LOBBY_MATCH / LOBBY_RESULTS */
    uint8_t starter;               /* console that pressed Start (phase STAGE) */
    lobby_rules rules;
    uint16_t stage;                /* the picked stage (phase STAGE once picked, and MATCH) */
    uint8_t stage_picked, alt_stage;
    uint8_t mode;                  /* 0 normal, 1 sudden death (second game of the same session) */
    uint8_t sss_picker;            /* phase STAGE: the port whose console picks the stage (LOBBY_PICK_ANY: any), as
                                    * the host's Stage Selection rule decided it when Start was pressed: Turns and
                                    * Loser a player's port, Random and Ordered the host (its stage select picks at
                                    * once), Any LOBBY_PICK_ANY. Passes on when that console leaves. */
    uint8_t busy;                  /* bit n: the console on port n is busy (its name tag list, name entry, the host's
                                    * rules menu or a held slider); READY is hidden everywhere and Start is refused
                                    * while any bit is set */
    uint8_t busy_why[LOBBY_SLOTS]; /* why port n is busy (LOBBY_BUSY_*), 0 when not; busy bit n == (busy_why[n] != 0) */
    uint8_t in_use;                /* bit n: door n is in use on some console (a slider held, its name tag list open,
                                    * its console in name entry or holding something): as on one GameCube, nobody else
                                    * may switch it between Human, CPU and closed meanwhile */
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
    /* Consoles that played the running match (or the results up) and have left it: bit n, with the name the console on
     * port n had when the match began. A console on that port now (one that joined since) did not play, so the
     * match overview of a console watching shows the name of the one that left until the lobby reopens. */
    uint8_t gone;
    char gone_name[LOBBY_SLOTS][LOBBY_NAME_BYTES];
} lobby_state;

/* ---- the wire ---- */
int lobby_encode(const lobby_state *s, uint8_t *out, int cap);          /* bytes written, 0 if cap is too small */
int lobby_decode(lobby_state *s, const uint8_t *in, int len);           /* 0 ok, -1 not a lobby blob */

/* ---- the beacon ----
 * What a console browsing for games sees of a lobby (lw_host_set_beacon), apart from the lobby blob: the phase, the
 * doors (during a match, the fighter the match shows now and Human/CPU as it plays) and, when the host has one, its Mii
 * as CFLStoreData (MiiData, 2 bytes of padding, CRC-16 big-endian), checked on arrival. 20 or 116 bytes. */
#define LOBBY_MII_BYTES 96
#define LOBBY_BEACON_MAX (4 + 4 * LOBBY_SLOTS + LOBBY_MII_BYTES)
typedef struct {
    uint8_t phase;                 /* LOBBY_CSS / STAGE / MATCH / RESULTS */
    uint8_t teams;                 /* the rules say Teams: a door's colour is its team's */
    struct {
        uint8_t kind;              /* LOBBY_HUMAN / LOBBY_CPU / LOBBY_EMPTY */
        uint8_t team;              /* 0 red, 1 blue, 2 green */
        uint8_t fighter, costume;  /* external id and costume */
        uint8_t chosen;            /* 1: a fighter is placed (always during a match) */
    } slot[LOBBY_SLOTS];
} lobby_beacon;
/* mii: the host's CFLStoreData (LOBBY_MII_BYTES) or NULL. Bytes written, 0 if cap is too small. */
int lobby_encode_beacon(const lobby_state *s, const uint8_t *mii, uint8_t *out, int cap);
/* 0 ok, -1 not a beacon. mii (LOBBY_MII_BYTES) receives the Mii; *has_mii is 1 only when one came and its CRC holds. */
int lobby_decode_beacon(lobby_beacon *b, uint8_t *mii, int *has_mii, const uint8_t *in, int len);
/* CFL's CRC-16 (CCITT polynomial, seed 0, as cfl-mii's cflComputeCRC16). */
uint16_t lobby_crc16(const uint8_t *data, int len);
/* A Mii's CRC holds (the CRC over all LOBBY_MII_BYTES, its own two included, is 0). */
int lobby_mii_ok(const uint8_t *mii);
/* Write the CRC into a Mii (bytes 94 and 95). */
void lobby_mii_seal(uint8_t *mii);

/* A console's commands to the host. Each returns its length. */
enum { LCMD_DOOR = 1, LCMD_START = 2, LCMD_STAGE = 3, LCMD_BACK = 4, LCMD_BUSY = 5, LCMD_RESULTS = 6 };
/* Set door `slot` (any console may edit any door). base: the door's kind as the sender last saw it, plus 1 (0: not
 * known). An edit that keeps that kind while the door's kind has changed since (another console switched it) is
 * dropped: it was made from an older view and would undo that switch. */
int lobby_cmd_door(uint8_t *out, int slot, const lobby_slot *s, int base);
int lobby_cmd_start(uint8_t *out);                                      /* Start from the CSS: go to the stage select */
int lobby_cmd_stage(uint8_t *out, int stage, int alt_stage);            /* a stage pick */
int lobby_cmd_back(uint8_t *out);                                       /* B at the stage select: back to the CSS */
/* The sender is busy (a LOBBY_BUSY_* code; 0 no longer), and which doors it is using (bit n: door n; see
 * lobby_state.in_use). An unknown code reads as LOBBY_BUSY_SLIDER (busy, no reason shown). */
int lobby_cmd_busy(uint8_t *out, int busy, int doors);
int lobby_cmd_results(uint8_t *out, int ready, int advance);            /* results screen: the sender's ready, and how far a
                                                                         * press moved it on (0: no change) */

/* ---- the host ---- */
typedef struct {
    lobby_state st;
    unsigned present;              /* consoles the host last synced with */
    unsigned joined_in_match;      /* arrived during a match: their door is set when it ends */
    unsigned left_in_match;
    unsigned left_played;          /* ports whose console played the running match (or the results up) and left it: a
                                    * console that takes such a port meanwhile did not play, and the port counts as ready */
    uint8_t last_tag[LOBBY_SLOTS][LOBBY_TAG_BYTES];   /* each port's last Human tag (a change clears its stars) */
    uint8_t last_tag_ok[LOBBY_SLOTS];
    uint8_t in_use_by[LOBBY_SLOTS];   /* the doors each port's console is using; st.in_use is their OR */
    char played_name[LOBBY_SLOTS][LOBBY_NAME_BYTES];   /* the names of the consoles in the running match (lobby_host_name) */
    int8_t turn;                   /* Stage Selection Turns: the port that picked last (-1: none yet; vanilla's vs.loser) */
    int8_t loser;                  /* Stage Selection Loser: the last match's loser as the host's engine decided it (-1:
                                    * none yet; vanilla's vs.winner, from gm_80168710) */
    uint32_t rng;                  /* the host's own random numbers (Loser's first pick); seed it as you like */
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
int lobby_host_set_door(lobby_host *h, int slot, const lobby_slot *s);       /* a door as it is to be, no checks */
/* Console `from`'s edit of door `slot` (the host's own console's too): refused while another console uses that door
 * and the edit switches it between Human, CPU and closed, and dropped when made from an older view (base, see
 * lobby_cmd_door). Then as lobby_host_set_door. */
int lobby_host_edit_door(lobby_host *h, int from, int slot, const lobby_slot *s, int base);
int lobby_host_start(lobby_host *h, int from);                               /* the host's own Start press */
/* Console `from`'s stage pick (the host's own console is port 0). Only the picker may pick (sss_picker), and the first
 * pick wins. LOBBY_STAGE_RANDOM is taken as the pick but starts nothing: the host's console then picks the stage from
 * its save and sends it from port 0, which replaces the random pick and starts the match. */
int lobby_host_pick_stage(lobby_host *h, int from, int stage, int alt_stage);
/* Back from the stage select (console `from`): only the picker may, and not once a stage is picked. */
int lobby_host_back(lobby_host *h, int from);
int lobby_host_set_rules(lobby_host *h, const lobby_rules *r);      /* the host changed the rules */
int lobby_host_set_busy(lobby_host *h, int port, int busy);                  /* a console's busy code (its own only) */
int lobby_host_set_in_use(lobby_host *h, int port, int doors);               /* the doors a console is using */
/* Phase MATCH; call after lw_host_start. `again`: a follow-up game (sudden death) with the consoles of the last one. */
void lobby_host_match_began(lobby_host *h, int again);
/* The name of the console on `port` as the match begins (call for each port after lobby_host_match_began): if it
 * leaves before the lobby reopens, the lobby keeps showing it (lobby_state.gone). */
void lobby_host_name(lobby_host *h, int port, const char *name);
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
/* The host's engine decided the last match's loser (vanilla's gm_80168710, its VsModeData.winner; -1 none), for the
 * Loser Stage Selection rule. */
void lobby_host_set_loser(lobby_host *h, int port);

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
