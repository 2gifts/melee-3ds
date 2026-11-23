/* The Melee lobby of a local wireless group: who sits in which of the four doors of the character select
 * screen, the host's rules summary and the stage pick. Pure data and rules, no I/O: slippi_local.c moves it through
 * the localwireless module (the host's lobby blob, consoles' commands) and slippi_bridge.c turns it into the match
 * block. The host owns the state and applies commands in arrival order; everyone else shows the host's snapshot.
 *
 * Four-GameCube-ports model: console n is port n; door n is port n's fighter and is Human, CPU or empty whether or
 * not a console sits on that port. */
#ifndef SLIPPI_LOBBY_H
#define SLIPPI_LOBBY_H
#include <stdint.h>

#define LOBBY_SLOTS 4
#define LOBBY_BLOB_MAX 96          /* LW_MAX_LOBBY */
#define LOBBY_TAG_BYTES 8          /* a name tag's game-encoded text (gm/types.h namedata) */
#define LOBBY_BLOCK_SIZE 0x140     /* match block + trailer (SLIPPI_NET_MATCH_BLOCK_TOTAL rounded for the 3 flag bytes) */

enum { LOBBY_HUMAN = 0, LOBBY_CPU = 1, LOBBY_EMPTY = 2 };
enum { LOBBY_CSS = 0, LOBBY_STAGE = 1, LOBBY_MATCH = 2 };

typedef struct {
    uint8_t kind;                  /* LOBBY_HUMAN / LOBBY_CPU / LOBBY_EMPTY */
    uint8_t fighter;               /* external character id, meaningful when chosen */
    uint8_t color;                 /* costume */
    uint8_t team;                  /* 0 red, 1 blue, 2 green (used when the rules say Teams) */
    uint8_t level;                 /* CPU level 1..9 */
    uint8_t chosen;                /* 0 none, 1 a fighter is placed in the door, 2 the token is held over a fighter (preview), 3 held over nothing */
    uint8_t tag[LOBBY_TAG_BYTES];  /* name tag text, all zero = none */
} lobby_slot;

/* What the host's rules say, for display everywhere (the full rules go in the match block). */
typedef struct {
    uint8_t time_mode;             /* 0 stock, 1 time */
    uint8_t stocks;
    uint8_t minutes;
    uint8_t items;                 /* 0 off, else the item frequency */
    uint8_t teams;
    uint8_t friendly_fire;
} lobby_rules;

typedef struct {
    uint8_t phase;                 /* LOBBY_CSS / LOBBY_STAGE / LOBBY_MATCH */
    uint8_t starter;               /* console that pressed Start (phase STAGE) */
    lobby_rules rules;
    uint16_t stage;                /* the picked stage (phase STAGE once picked, and MATCH) */
    uint8_t stage_picked, alt_stage;
    uint8_t mode;                  /* 0 normal, 1 sudden death (second game of the same session) */
    lobby_slot slot[LOBBY_SLOTS];
} lobby_state;

/* ---- the wire ---- */
int lobby_encode(const lobby_state *s, uint8_t *out, int cap);          /* bytes written, 0 if cap is too small */
int lobby_decode(lobby_state *s, const uint8_t *in, int len);           /* 0 ok, -1 not a lobby blob */

/* A console's commands to the host. Each returns its length. */
enum { LCMD_DOOR = 1, LCMD_START = 2, LCMD_STAGE = 3, LCMD_BACK = 4 };
int lobby_cmd_door(uint8_t *out, int slot, const lobby_slot *s);       /* set door `slot` (any console may edit any door) */
int lobby_cmd_start(uint8_t *out);                                      /* Start from the CSS: go to the stage select */
int lobby_cmd_stage(uint8_t *out, int stage, int alt_stage);            /* a stage pick */
int lobby_cmd_back(uint8_t *out);                                       /* B at the stage select: back to the CSS */

/* ---- the host ---- */
typedef struct {
    lobby_state st;
    unsigned present;              /* consoles the host last synced with */
    unsigned joined_in_match;      /* arrived during a match: their door is set when it ends */
    unsigned left_in_match;
} lobby_host;

enum { LOBBY_CHANGED = 1, LOBBY_STAGE_REACHED = 2, LOBBY_START_MATCH = 4 };

void lobby_host_init(lobby_host *h, unsigned present);
/* Consoles joined or left (the module's present mask). A console leaving turns its Human door into a CPU level 3 (as a
 * controller unplugged on a GameCube), a console arriving on an empty port makes it Human with no fighter yet; a CPU
 * door stays a CPU. During a match this waits for lobby_host_match_over. Returns LOBBY_CHANGED if the state did. */
int lobby_host_sync(lobby_host *h, unsigned present);
/* One command from console `from` (never trust the contents: bad ones change nothing). Returns the LOBBY_* flags:
 * LOBBY_START_MATCH when the stage pick that ends the stage select arrived, so the caller builds the match block. */
int lobby_host_command(lobby_host *h, int from, const uint8_t *cmd, int len);
int lobby_host_set_door(lobby_host *h, int slot, const lobby_slot *s);       /* the host's own console edits */
int lobby_host_start(lobby_host *h, int from);                               /* the host's own Start press */
int lobby_host_pick_stage(lobby_host *h, int stage, int alt_stage);          /* the host's own stage pick */
int lobby_host_set_rules(lobby_host *h, const lobby_rules *r);      /* the host changed the rules */
void lobby_host_match_began(lobby_host *h);                                  /* phase MATCH; call after lw_host_start */
int lobby_host_match_over(lobby_host *h);                                    /* phase CSS again; applies waiting joins/leaves */

/* Can Start take the lobby to the stage select? at least two doors in use and a fighter in every one. */
int lobby_can_start(const lobby_state *s);

/* ---- the match ---- */
/* Fills out[0..0x13F]: the game info block (the Slippi template, the rules summary and a door per player), and the
 * trailer: rng_offset, local index (the caller overwrites it per console), delay, alt stage mode. Only the rules the
 * lobby carries (mode, stocks, minutes, items, teams, friendly fire) are applied; the rest are the Slippi defaults. */
void lobby_build_block(unsigned char *out, const lobby_state *s, unsigned rng_offset, int delay);

/* The template of a 1v1 DIRECT block (slippi_bridge.c). */
extern const unsigned char slippi_match_block_template[0x138];

#endif
