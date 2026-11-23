/* The Melee lobby of a local wireless group: see slippi_lobby.h. Pure logic, no I/O, so the PC tests drive it directly
 * (tools/slippi/src/lobby_unit.c). */
#include "slippi_lobby.h"
#include <string.h>

/* Bytes 8 and 9 are 0 as in Slippi Ishiiruka (what Slippi Launcher ships;
 * mainline has 1, 1 there): the PC's replays record 0 at 0x9. */
/* CEXISlippi::prepareOnlineMatchState's static block: "a VS match with P1 Red
 * Falco vs P2 Red Bowser vs P3 Young Link vs P4 Young Link on Battlefield". */
const unsigned char slippi_match_block_template[0x138] = {
    0x32, 0x01, 0x86, 0x4C, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x6E, 0x00,
    0x1F, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x3F,
    0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x78, 0x00, 0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x05, 0x00, 0x04,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00, 0xC0, 0x00, 0x04, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F,
    0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00,
    0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x09, 0x00, 0x78, 0x00, 0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
    0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00, 0x40, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09,
    0x00, 0x78, 0x00, 0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
};

#define BLOB_ID 0x4C
#define BLOB_HEADER 12
#define SLOT_BYTES 14
#define MAX_FIGHTER 0x19            /* external character ids 0..0x19 (Melee's 26 fighters) */
#define DEFAULT_CPU_LEVEL 3         /* the level a dropped console's fighter plays at (online.c take_over_slot) */

/* ---- the wire ---- */

int lobby_encode(const lobby_state *s, uint8_t *out, int cap)
{
    const int need = BLOB_HEADER + SLOT_BYTES * LOBBY_SLOTS;
    if (cap < need) return 0;
    out[0] = BLOB_ID;
    out[1] = s->phase;
    out[2] = s->starter;
    out[3] = s->mode;
    out[4] = (uint8_t)((s->rules.time_mode & 1) | (s->rules.teams & 1) << 1 | (s->rules.friendly_fire & 1) << 2);
    out[5] = s->rules.stocks;
    out[6] = s->rules.minutes;
    out[7] = s->rules.items;
    out[8] = (uint8_t)(s->stage >> 8);
    out[9] = (uint8_t)s->stage;
    out[10] = s->stage_picked;
    out[11] = s->alt_stage;
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        uint8_t *p = out + BLOB_HEADER + SLOT_BYTES * i;
        const lobby_slot *l = &s->slot[i];
        p[0] = l->kind; p[1] = l->fighter; p[2] = l->color; p[3] = l->team; p[4] = l->level; p[5] = l->chosen;
        memcpy(p + 6, l->tag, LOBBY_TAG_BYTES);
    }
    return need;
}

/* Clamp a received slot to values the game can show. */
static void sanitize_slot(lobby_slot *l)
{
    if (l->kind > LOBBY_EMPTY) l->kind = LOBBY_EMPTY;
    if (l->fighter > MAX_FIGHTER) { l->fighter = 0; l->chosen = 0; }
    if (l->team > 2) l->team = 0;
    if (l->level < 1 || l->level > 9) l->level = DEFAULT_CPU_LEVEL;
    l->chosen = l->chosen > 3 ? 0 : l->chosen;   /* 2: the token is held over a fighter (a preview), 3: held over nothing */
    if (l->kind == LOBBY_EMPTY) l->chosen = 0;
}

int lobby_decode(lobby_state *s, const uint8_t *in, int len)
{
    if (len < BLOB_HEADER + SLOT_BYTES * LOBBY_SLOTS || in[0] != BLOB_ID) return -1;
    memset(s, 0, sizeof(*s));
    s->phase = in[1] > LOBBY_MATCH ? LOBBY_CSS : in[1];
    s->starter = in[2] & 3;
    s->mode = in[3] ? 1 : 0;
    s->rules.time_mode = in[4] & 1;
    s->rules.teams = (in[4] >> 1) & 1;
    s->rules.friendly_fire = (in[4] >> 2) & 1;
    s->rules.stocks = in[5];
    s->rules.minutes = in[6];
    s->rules.items = in[7];
    s->stage = (uint16_t)((in[8] << 8) | in[9]);
    s->stage_picked = in[10] ? 1 : 0;
    s->alt_stage = in[11];
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        const uint8_t *p = in + BLOB_HEADER + SLOT_BYTES * i;
        lobby_slot *l = &s->slot[i];
        l->kind = p[0]; l->fighter = p[1]; l->color = p[2]; l->team = p[3]; l->level = p[4]; l->chosen = p[5];
        memcpy(l->tag, p + 6, LOBBY_TAG_BYTES);
        sanitize_slot(l);
    }
    return 0;
}

int lobby_cmd_door(uint8_t *out, int slot, const lobby_slot *s)
{
    out[0] = LCMD_DOOR; out[1] = (uint8_t)slot;
    out[2] = s->kind; out[3] = s->fighter; out[4] = s->color; out[5] = s->team; out[6] = s->level; out[7] = s->chosen;
    memcpy(out + 8, s->tag, LOBBY_TAG_BYTES);
    return 16;
}
int lobby_cmd_start(uint8_t *out) { out[0] = LCMD_START; return 1; }
int lobby_cmd_stage(uint8_t *out, int stage, int alt_stage)
{
    out[0] = LCMD_STAGE; out[1] = (uint8_t)(stage >> 8); out[2] = (uint8_t)stage; out[3] = (uint8_t)alt_stage;
    return 4;
}
int lobby_cmd_back(uint8_t *out) { out[0] = LCMD_BACK; return 1; }

/* ---- the host ---- */

static void reset_slot(lobby_slot *l, int kind)
{
    memset(l, 0, sizeof(*l));
    l->kind = (uint8_t)kind;
    l->level = DEFAULT_CPU_LEVEL;
}

static void default_rules(lobby_rules *r)
{
    memset(r, 0, sizeof(*r));
    r->stocks = 4;
    r->minutes = 8;
}

void lobby_host_init(lobby_host *h, unsigned present)
{
    memset(h, 0, sizeof(*h));
    default_rules(&h->st.rules);
    for (int i = 0; i < LOBBY_SLOTS; ++i) reset_slot(&h->st.slot[i], LOBBY_EMPTY);
    h->present = 0;
    lobby_host_sync(h, present);
}

static int console_arrives(lobby_host *h, int i)
{
    lobby_slot *l = &h->st.slot[i];
    if (l->kind != LOBBY_EMPTY) return 0;         /* a CPU waits for its player to click the CPU label */
    reset_slot(l, LOBBY_HUMAN);
    return 1;
}

static int console_leaves(lobby_host *h, int i)
{
    lobby_slot *l = &h->st.slot[i];
    if (l->kind != LOBBY_HUMAN) return 0;
    l->kind = LOBBY_CPU;                           /* a controller unplugged: its fighter plays on as a CPU */
    l->level = DEFAULT_CPU_LEVEL;
    memset(l->tag, 0, sizeof(l->tag));             /* a CPU has no tag */
    if (l->chosen != 1) { l->kind = LOBBY_EMPTY; l->chosen = 0; }   /* nothing in the door to keep playing */
    return 1;
}

int lobby_host_sync(lobby_host *h, unsigned present)
{
    int changed = 0;
    unsigned diff = (present ^ h->present) & 0xF;
    if (!diff) return 0;
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        unsigned bit = 1u << i;
        if (!(diff & bit)) continue;
        if (h->st.phase == LOBBY_MATCH) {          /* the running game owns the doors: decide when it ends */
            if (present & bit) { h->joined_in_match |= bit; h->left_in_match &= ~bit; }
            else { h->left_in_match |= bit; h->joined_in_match &= ~bit; }
            continue;
        }
        changed |= (present & bit) ? console_arrives(h, i) : console_leaves(h, i);
    }
    h->present = present & 0xF;
    if (h->st.phase == LOBBY_STAGE && !(present & (1u << h->st.starter))) { h->st.phase = LOBBY_CSS; h->st.stage_picked = 0; changed = 1; }
    return changed ? LOBBY_CHANGED : 0;
}

int lobby_can_start(const lobby_state *s)
{
    int used = 0;
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        const lobby_slot *l = &s->slot[i];
        if (l->kind == LOBBY_EMPTY) continue;
        if (l->chosen != 1) return 0;
        ++used;
    }
    return used >= 2;
}

int lobby_host_set_door(lobby_host *h, int slot, const lobby_slot *s)
{
    if (slot < 0 || slot >= LOBBY_SLOTS || h->st.phase != LOBBY_CSS) return 0;
    lobby_slot n = *s;
    sanitize_slot(&n);
    if (n.kind != LOBBY_HUMAN) memset(n.tag, 0, sizeof(n.tag));       /* only a Human door shows a name tag */
    if (n.kind == LOBBY_HUMAN && !(h->present & (1u << slot)) && h->st.slot[slot].kind != LOBBY_HUMAN) n.kind = LOBBY_CPU;   /* no console there */
    if (!memcmp(&n, &h->st.slot[slot], sizeof(n))) return 0;
    h->st.slot[slot] = n;
    return LOBBY_CHANGED;
}

int lobby_host_start(lobby_host *h, int from)
{
    if (h->st.phase != LOBBY_CSS || !lobby_can_start(&h->st)) return 0;
    h->st.phase = LOBBY_STAGE;
    h->st.starter = (uint8_t)(from & 3);
    h->st.stage_picked = 0;
    return LOBBY_CHANGED | LOBBY_STAGE_REACHED;
}

int lobby_host_pick_stage(lobby_host *h, int stage, int alt_stage)
{
    if (h->st.phase != LOBBY_STAGE || h->st.stage_picked) return 0;   /* the first pick wins */
    h->st.stage = (uint16_t)stage;
    h->st.alt_stage = (uint8_t)alt_stage;
    h->st.stage_picked = 1;
    return LOBBY_CHANGED | LOBBY_START_MATCH;
}

int lobby_host_set_rules(lobby_host *h, const lobby_rules *r)
{
    if (!memcmp(r, &h->st.rules, sizeof(*r))) return 0;
    h->st.rules = *r;
    return LOBBY_CHANGED;
}

int lobby_host_command(lobby_host *h, int from, const uint8_t *cmd, int len)
{
    if (len < 1 || from < 0 || from >= LOBBY_SLOTS) return 0;
    switch (cmd[0]) {
        case LCMD_DOOR: {
            if (len < 16) return 0;
            lobby_slot s;
            memset(&s, 0, sizeof(s));
            s.kind = cmd[2]; s.fighter = cmd[3]; s.color = cmd[4]; s.team = cmd[5]; s.level = cmd[6]; s.chosen = cmd[7];
            memcpy(s.tag, cmd + 8, LOBBY_TAG_BYTES);
            /* A console that sends a command is here, even if the roster has not told the host yet. */
            unsigned saved = h->present;
            h->present |= 1u << from;
            int r = lobby_host_set_door(h, cmd[1], &s);
            h->present = saved;
            return r;
        }
        case LCMD_START: return lobby_host_start(h, from);
        case LCMD_STAGE:
            if (len < 4) return 0;
            return lobby_host_pick_stage(h, (cmd[1] << 8) | cmd[2], cmd[3]);
        case LCMD_BACK:
            if (h->st.phase != LOBBY_STAGE || h->st.stage_picked) return 0;
            h->st.phase = LOBBY_CSS;
            return LOBBY_CHANGED;
        default: return 0;
    }
}

void lobby_host_match_began(lobby_host *h)
{
    h->st.phase = LOBBY_MATCH;
    h->joined_in_match = h->left_in_match = 0;
}

int lobby_host_match_over(lobby_host *h)
{
    unsigned now = h->present;                    /* what the module last told us; a join or leave in the match is applied now */
    h->st.phase = LOBBY_CSS;
    h->st.stage_picked = 0;
    h->st.mode = 0;
    unsigned joined = h->joined_in_match, left = h->left_in_match;
    h->joined_in_match = h->left_in_match = 0;
    int changed = LOBBY_CHANGED;
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        unsigned bit = 1u << i;
        if (left & bit) console_leaves(h, i);
        if (joined & bit) console_arrives(h, i);
    }
    h->present = now;
    return changed;
}

/* ---- the match block ---- */

static void put_u16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

void lobby_build_block(unsigned char *b, const lobby_state *s, unsigned rng_offset, int delay)
{
    memcpy(b, slippi_match_block_template, 0x138);
    memset(b + 0x138, 0, LOBBY_BLOCK_SIZE - 0x138);
    const lobby_rules *r = &s->rules;
    int stocks = r->stocks >= 1 && r->stocks <= 99 ? r->stocks : 4;
    int minutes = r->minutes >= 1 && r->minutes <= 99 ? r->minutes : 8;
    int teams = r->teams ? 1 : 0;

    /* The doors. The nametag stays 0x78 (none) for every slot: the shared block carries no save-dependent id, a
     * console that has chosen a tag shows it for its own slot, and the others come from the lobby (engine side). */
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        const lobby_slot *l = &s->slot[i];
        unsigned char *p = b + 0x60 + 0x24 * i;
        int used = l->kind != LOBBY_EMPTY && l->chosen == 1;
        p[1] = !used ? 3 : l->kind == LOBBY_HUMAN ? 0 : 1;
        p[0xA] = 0x78;
        p[0xC] = used && l->kind == LOBBY_HUMAN ? 0xC0 : 0x40;
        p[5] = (unsigned char)i;                    /* spawn point */
        p[9] = 0;
        if (!used) continue;
        p[0] = l->fighter;
        p[2] = (unsigned char)stocks;
        p[3] = l->color;
        p[7] = 0;
        p[9] = teams ? l->team : 0;
        p[0xE] = 4;
        p[0xF] = l->kind == LOBBY_CPU ? l->level : 1;
        p[0x10] = p[0x11] = 0;
    }
    /* Shades for the same character and costume (Sheik counts as Zelda). */
    unsigned keys[4], counts[4];
    int nkeys = 0;
    for (int i = 0; i < LOBBY_SLOTS; ++i) {
        unsigned char *p = b + 0x60 + 0x24 * i;
        if (p[1] == 3) continue;
        unsigned char ch = p[0];
        if (ch == 0x13) ch = 0x12;
        unsigned key = ((unsigned)ch << 8) | p[3];
        int k = 0;
        while (k < nkeys && keys[k] != key) ++k;
        if (k == nkeys) { keys[nkeys] = key; counts[nkeys] = 0; ++nkeys; }
        p[7] = (unsigned char)counts[k];
        counts[k] += 1;
    }

    /* Rules. Byte 0 0x32 is a stock match with the timer on; a timed match clears is_stock (byte 2, top bit).
     * UNVERIFIED against a real vanilla time-mode block and the item mask: check against a real match in Azahar. */
    b[0x0] = 0x32;
    b[0x3] = 0x4C;
    b[2] &= 0xF7;                                   /* vanilla pause allowed */
    if (r->time_mode) b[2] &= 0x7F;                 /* timed match: not a stock match */
    b[0x1] = (unsigned char)((b[0x1] & 0xFE) | (!teams || r->friendly_fire));   /* friendly fire only matters in Teams */
    b[0x8] = (unsigned char)teams;
    b[0xE] = (unsigned char)(s->stage >> 8);
    b[0xF] = (unsigned char)s->stage;
    b[0x10] = 0; b[0x11] = 0;
    put_u16(b + 0x12, (unsigned)(minutes * 60));
    b[0xB] = 0xFF;
    unsigned long long items = 0xF80000000F000000ull;
    if (r->items) { items |= 0x0FFFFFFFull << 28; b[0xB] = (unsigned char)(r->items - 1); }
    for (int i = 0; i < 8; ++i) b[0x23 + i] = (unsigned char)(items >> (56 - 8 * i));

    b[0x138] = (unsigned char)(rng_offset >> 24);
    b[0x139] = (unsigned char)(rng_offset >> 16);
    b[0x13A] = (unsigned char)(rng_offset >> 8);
    b[0x13B] = (unsigned char)rng_offset;
    b[0x13C] = 0;                                   /* the local index: the caller sets it per console */
    b[0x13D] = (unsigned char)(delay < 1 ? 1 : delay > 15 ? 15 : delay);
    b[0x13E] = s->alt_stage;
    b[0x13F] = 0;
}
