/* Unit test for the Melee lobby rules (port/3ds/group/group_lobby.c): wire round trips, doors, who may edit which door,
 * joins and leaves, start and stage arbitration, and the match block. Prints "lobby_unit: ok" or the first failure. */
#include "group_lobby.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("lobby_unit: FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static lobby_slot fighter(int kind, int ch, int color)
{
    lobby_slot s;
    memset(&s, 0, sizeof(s));
    s.kind = (uint8_t)kind; s.fighter = (uint8_t)ch; s.color = (uint8_t)color; s.chosen = 1; s.level = 5;
    return s;
}

static int door_base(lobby_host *h, int from, int slot, lobby_slot s, int base)
{
    uint8_t c[LOBBY_BLOB_MAX];
    int n = lobby_cmd_door(c, slot, &s, base);
    return lobby_host_command(h, from, c, n);
}

static int busy_cmd(lobby_host *h, int from, int busy, int doors)
{
    uint8_t c[8];
    return lobby_host_command(h, from, c, lobby_cmd_busy(c, busy, doors));
}

static int door(lobby_host *h, int from, int slot, lobby_slot s)
{
    uint8_t c[LOBBY_BLOB_MAX];
    int n = lobby_cmd_door(c, slot, &s, 0);
    return lobby_host_command(h, from, c, n);
}

int main(void)
{
    lobby_host h;
    uint8_t blob[LOBBY_BLOB_MAX], cmd[24];
    lobby_state d;

    /* host alone: every door closed (its console has not opened its own yet); cannot start */
    lobby_host_init(&h, 1);
    CHECK(h.st.slot[0].kind == LOBBY_EMPTY && h.st.slot[1].kind == LOBBY_EMPTY);
    CHECK(!lobby_can_start(&h.st));
    CHECK(door(&h, 0, 0, fighter(LOBBY_HUMAN, 0x14, 1)) & LOBBY_CHANGED);
    CHECK(!door(&h, 0, 0, fighter(LOBBY_HUMAN, 0x14, 1)));                       /* no change, no flag */
    CHECK(!lobby_host_command(&h, 0, (cmd[0] = LCMD_START, cmd), 1));            /* one door */
    /* a CPU on a port with no console */
    CHECK(door(&h, 0, 2, fighter(LOBBY_CPU, 0x02, 0)) & LOBBY_CHANGED);
    CHECK(h.st.slot[2].kind == LOBBY_CPU && h.st.slot[2].level == 5);
    /* asking for Human on a port with no console turns it into a CPU */
    CHECK(door(&h, 0, 3, fighter(LOBBY_HUMAN, 0x0A, 0)) & LOBBY_CHANGED);
    CHECK(h.st.slot[3].kind == LOBBY_CPU);
    /* garbage is ignored or clamped */
    cmd[0] = LCMD_DOOR; cmd[1] = 9; memset(cmd + 2, 0xEE, 15);
    CHECK(!lobby_host_command(&h, 0, cmd, 17));
    cmd[1] = 1;
    CHECK(!lobby_host_command(&h, 0, cmd, 16));                                   /* too short (no handicap byte) */
    lobby_host_command(&h, 0, cmd, 17);
    CHECK(h.st.slot[1].kind == LOBBY_EMPTY && h.st.slot[1].fighter == 0 && h.st.slot[1].handicap == 0);
    CHECK(!lobby_host_command(&h, 7, cmd, 17));
    /* a console the lobby has not seen arrive (or that has left) cannot act: no door edit, Start or busy bit */
    {
        lobby_slot open = fighter(LOBBY_HUMAN, 0, 0);
        open.chosen = 0;
        CHECK(!door(&h, 1, 1, open) && h.st.slot[1].kind == LOBBY_EMPTY);
        CHECK(!door(&h, 1, 2, fighter(LOBBY_EMPTY, 0, 0)) && h.st.slot[2].kind == LOBBY_CPU);
        CHECK(!lobby_host_command(&h, 1, cmd, lobby_cmd_busy(cmd, 1, 0)) && h.st.busy == 0);
    }

    /* a console joins port 2 (index 1): joining opens no door; its CSS opens it with an edit, as a player would */
    CHECK(lobby_host_sync(&h, 0x3) == 0 && h.st.slot[1].kind == LOBBY_EMPTY);
    {
        lobby_slot open = fighter(LOBBY_HUMAN, 0, 0);
        open.chosen = 0;
        CHECK(door(&h, 1, 1, open) & LOBBY_CHANGED);
    }
    CHECK(h.st.slot[1].kind == LOBBY_HUMAN && !h.st.slot[1].chosen);
    CHECK(!lobby_can_start(&h.st));
    CHECK(lobby_host_sync(&h, 0x7) == 0 && h.st.slot[2].kind == LOBBY_CPU);       /* joins onto a CPU door: it stays a CPU */
    /* the wire */
    CHECK(lobby_encode(&h.st, blob, sizeof(blob)) > 0 && lobby_encode(&h.st, blob, sizeof(blob)) <= LOBBY_BLOB_MAX);
    CHECK(lobby_decode(&d, blob, LOBBY_BLOB_MAX) == 0 && !memcmp(&d, &h.st, sizeof(d)));
    blob[0] = 0; CHECK(lobby_decode(&d, blob, LOBBY_BLOB_MAX) < 0);

    /* console 1 picks a fighter and a tag; the door only shows a tag when Human */
    lobby_slot s = fighter(LOBBY_HUMAN, 0x0C, 2);
    memcpy(s.tag, "ABCDEFGH", 8);
    CHECK(door(&h, 1, 1, s));
    CHECK(!memcmp(h.st.slot[1].tag, "ABCDEFGH", 8));
    s.kind = LOBBY_CPU;
    door(&h, 1, 1, s);
    CHECK(h.st.slot[1].kind == LOBBY_CPU && h.st.slot[1].tag[0] == 0);
    s.kind = LOBBY_HUMAN;
    door(&h, 1, 1, s);                                                             /* the player clicks the CPU label again */
    CHECK(h.st.slot[1].kind == LOBBY_HUMAN);
    /* another console cannot change the fighter, costume or tag of a Human door a console sits on (what a stray local
     * change on a mirrored door looks like); it may still make that door a CPU, as on a GameCube */
    lobby_slot other = fighter(LOBBY_HUMAN, 0x05, 3);
    door(&h, 0, 1, other);
    CHECK(h.st.slot[1].fighter == 0x0C && h.st.slot[1].color == 2 && !memcmp(h.st.slot[1].tag, "ABCDEFGH", 8));
    other.kind = LOBBY_CPU;
    CHECK(door(&h, 0, 1, other) & LOBBY_CHANGED);
    CHECK(h.st.slot[1].kind == LOBBY_CPU && h.st.slot[1].fighter == 0x0C && h.st.slot[1].color == 2);
    door(&h, 1, 1, s);                                                             /* its own console takes it back */
    CHECK(h.st.slot[1].kind == LOBBY_HUMAN && !memcmp(h.st.slot[1].tag, "ABCDEFGH", 8));
    CHECK(h.st.slot[3].kind == LOBBY_CPU);
    /* Start: any console; then the first stage pick wins, a second one is ignored */
    CHECK(lobby_can_start(&h.st));
    /* a busy console (name tag list, name entry, rules menu, held slider): READY waits everywhere, so Start is refused until it is back */
    CHECK(lobby_host_command(&h, 2, cmd, lobby_cmd_busy(cmd, 1, 0)) & LOBBY_CHANGED);
    CHECK(h.st.busy == 0x4 && !lobby_can_start(&h.st));
    CHECK(!lobby_host_command(&h, 1, (cmd[0] = LCMD_START, cmd), 1) && h.st.phase == LOBBY_CSS);
    CHECK(!lobby_host_command(&h, 2, cmd, 1));                                    /* too short */
    CHECK(lobby_encode(&h.st, blob, sizeof(blob)) > 0 && lobby_decode(&d, blob, LOBBY_BLOB_MAX) == 0 && d.busy == 0x4);
    CHECK(lobby_host_command(&h, 2, cmd, lobby_cmd_busy(cmd, 0, 0)) & LOBBY_CHANGED);
    CHECK(h.st.busy == 0 && lobby_can_start(&h.st));
    /* only the sender's own bit; a console that leaves takes its bit with it */
    CHECK(lobby_host_command(&h, 1, cmd, lobby_cmd_busy(cmd, 1, 0)) && h.st.busy == 0x2);
    CHECK(!lobby_host_command(&h, 7, cmd, lobby_cmd_busy(cmd, 1, 0)) && h.st.busy == 0x2);
    {
        lobby_host t = h;
        CHECK(lobby_host_sync(&t, 0x5) & LOBBY_CHANGED);
        CHECK(t.st.busy == 0);
    }
    CHECK(lobby_host_set_busy(&h, 1, 0) & LOBBY_CHANGED);
    CHECK(lobby_can_start(&h.st));
    CHECK(lobby_host_command(&h, 1, (cmd[0] = LCMD_START, cmd), 1) & LOBBY_STAGE_REACHED);
    CHECK(h.st.phase == LOBBY_STAGE && h.st.starter == 1);
    CHECK(!door(&h, 0, 0, fighter(LOBBY_HUMAN, 3, 0)));                            /* doors are frozen at the stage select */
    CHECK(!lobby_host_command(&h, 0, (cmd[0] = LCMD_START, cmd), 1));
    int n = lobby_cmd_stage(cmd, 0x20, 1);
    CHECK(lobby_host_command(&h, 2, cmd, n) & LOBBY_START_MATCH);
    n = lobby_cmd_stage(cmd, 0x1F, 0);
    CHECK(!lobby_host_command(&h, 0, cmd, n) && h.st.stage == 0x20 && h.st.alt_stage == 1);

    /* the block */
    unsigned char b[LOBBY_BLOCK_SIZE];
    h.st.rules.teams = 0;
    lobby_build_block(b, &h.st, 0x1234, 2);
    CHECK(b[0xE] == 0 && b[0xF] == 0x20);
    CHECK(b[0x60] == 0x14 && b[0x61] == 0 && b[0x63] == 1);                         /* P1 human Falco, costume 1 */
    CHECK(b[0x60 + 0x24] == 0x0C && b[0x61 + 0x24] == 0 && b[0x63 + 0x24] == 2);   /* P2 human */
    CHECK(b[0x60 + 0x48] == 0x02 && b[0x61 + 0x48] == 1 && b[0x6F + 0x48] == 5);   /* P3 CPU level 5 */
    CHECK(b[0x61 + 0x6C] == 1 && b[0x60 + 0x6C] == 0x0A);                          /* P4 CPU (the 0x0A) */
    CHECK(b[0x6A] == 0x78 && b[0x6A + 0x24] == 0x78);                              /* no save-dependent tag ids */
    CHECK(b[0x138] == 0 && b[0x13A] == 0x12 && b[0x13B] == 0x34 && b[0x13D] == 2 && b[0x13E] == 1);
    CHECK(b[0x8] == 0);
    CHECK(b[0xB] == 0xFF);                                                          /* items off */
    CHECK(b[0x2] & 0x80);                                                           /* a stock match */
    /* the host's own rules (StartMeleeRules as vanilla builds them): a 3-minute time match with friendly fire, no pause,
     * the score display, items at frequency 2 with a chosen switch, a self-destruct penalty and a 1.5x damage ratio */
    unsigned char tmpl[LOBBY_BLOCK_SIZE];
    memcpy(tmpl, b, sizeof(tmpl));
    static const uint8_t mask[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    lobby_rules *r = &h.st.rules;
    memset(r->melee, 0xEE, sizeof(r->melee));                                       /* everything else must be ignored */
    r->melee[0] = (uint8_t)((0 << 5) | 0x02);                                       /* time match, timer on */
    r->melee[1] = 0x01;                                                             /* friendly fire */
    r->melee[2] = 0x08;                                                             /* pause off */
    r->melee[3] = 0x80;                                                             /* score display */
    r->melee[0xB] = 2; r->melee[0xC] = 0xFF;
    r->melee[0x10] = 0; r->melee[0x11] = 0; r->melee[0x12] = 0; r->melee[0x13] = 180;
    memcpy(r->melee + 0x20, mask, 8);
    r->melee[0x30] = 0x3F; r->melee[0x31] = 0xC0; r->melee[0x32] = 0; r->melee[0x33] = 0;
    r->melee_valid = 1;
    lobby_build_block(b, &h.st, 0x1234, 2);
    CHECK((b[0] >> 5) == 0 && (b[0] & 0x02) && (b[0] & 0x1C) == (tmpl[0] & 0x1C));   /* kind and timer; x0_3 stays */
    CHECK((b[1] & 1) && (b[1] & 0xFE) == (tmpl[1] & 0xFE));
    CHECK(!(b[2] & 0x80) && (b[2] & 0x08) && (b[2] & 0x77) == (tmpl[2] & 0x77));       /* not a stock match; no pause */
    CHECK((b[3] & 0x80) && (b[3] & 0x7F) == (tmpl[3] & 0x7F));
    CHECK(b[4] == tmpl[4] && b[5] == tmpl[5] && b[6] == tmpl[6] && b[7] == tmpl[7]);  /* is_vs etc. from the template */
    CHECK(b[0xB] == 2 && b[0xC] == 0xFF && b[0x13] == 180 && b[0x10] == 0 && !memcmp(b + 0x20, mask, 8));
    CHECK(b[0x30] == 0x3F && b[0x31] == 0xC0 && !memcmp(b + 0x28, tmpl + 0x28, 8) && !memcmp(b + 0x34, tmpl + 0x34, 4));
    CHECK(b[0x8] == 0 && b[0xE] == 0 && b[0xF] == 0x20 && b[0x13F] == 0);           /* teams and stage from the lobby */
    CHECK(lobby_encode(&h.st, blob, sizeof(blob)) > 0 && lobby_decode(&d, blob, LOBBY_BLOB_MAX) == 0 &&
          d.rules.melee_valid && !memcmp(d.rules.melee, r->melee, sizeof(r->melee)));
    r->melee[0] = (uint8_t)(1 << 5);                                                /* a stock match is_stock again */
    lobby_build_block(b, &h.st, 0, 2);
    CHECK((b[2] & 0x80) && !(b[0] & 0x02));
    r->melee_valid = 0;
    /* Teams: Start needs two different teams among the doors in use */
    {
        lobby_state t = h.st;
        t.phase = LOBBY_CSS;
        t.rules.teams = 1;
        for (int i = 0; i < LOBBY_SLOTS; ++i) t.slot[i].team = 2;
        CHECK(lobby_can_start(&t) == 0);
        t.slot[1].team = 0;
        CHECK(lobby_can_start(&t));
        t.rules.teams = 0;
        for (int i = 0; i < LOBBY_SLOTS; ++i) t.slot[i].team = 2;
        CHECK(lobby_can_start(&t));
    }
    h.st.rules.teams = 1; h.st.slot[0].team = 1;
    lobby_build_block(b, &h.st, 0, 40);
    CHECK(b[0x8] == 1 && b[0x69] == 1 && b[0x13D] == 15);                           /* delay clamped */
    /* a same character and costume pair gets shades */
    h.st.rules.teams = 0;
    h.st.slot[1].fighter = 0x14; h.st.slot[1].color = 1;
    lobby_build_block(b, &h.st, 0, 2);
    CHECK(b[0x67] == 0 && b[0x67 + 0x24] == 1);
    /* an empty door is "no player" */
    h.st.slot[3].kind = LOBBY_EMPTY;
    lobby_build_block(b, &h.st, 0, 2);
    CHECK(b[0x61 + 0x6C] == 3);

    /* a console leaves during the stage select... */
    h.st.phase = LOBBY_STAGE; h.st.stage_picked = 0; h.st.starter = 1;
    CHECK(lobby_host_sync(&h, 0x5) & LOBBY_CHANGED);                               /* the starter left */
    CHECK(h.st.phase == LOBBY_CSS && h.st.slot[1].kind == LOBBY_CPU);              /* ...its Human door plays on as a CPU */
    {   /* a door edit it queued before leaving is not applied over that CPU */
        lobby_slot late = fighter(LOBBY_HUMAN, 0x0C, 2);
        CHECK(!door(&h, 1, 1, late) && h.st.slot[1].kind == LOBBY_CPU);
    }

    /* ...and in a match: joins and leaves wait for the end */
    h.st.slot[1] = fighter(LOBBY_EMPTY, 0, 0); h.st.slot[1].chosen = 0;
    h.st.slot[3] = fighter(LOBBY_CPU, 4, 0);
    lobby_host_match_began(&h, 0);
    CHECK(h.st.phase == LOBBY_MATCH);
    CHECK(lobby_host_sync(&h, 0x7) == 0 && h.st.slot[1].kind == LOBBY_EMPTY);       /* late joiner: not touched yet */
    CHECK((lobby_host_sync(&h, 0x6) & LOBBY_CHANGED) && h.st.slot[0].kind == LOBBY_HUMAN && h.st.gone == 0x1);   /* slot 0 left:
                                                                                      * still Human during the match; its name is kept */
    /* the running match's status rides at the end of the blob, for consoles that joined during it */
    lobby_status ms;
    memset(&ms, 0, sizeof(ms));
    ms.timer = 1; ms.seconds = 431; ms.port[0].kind = 0; ms.port[0].stocks = 3; ms.port[0].damage = 287; ms.port[2].kind = 1; ms.port[3].kind = 3;
    CHECK(lobby_host_set_status(&h, &ms) & LOBBY_CHANGED);
    CHECK(!lobby_host_set_status(&h, &ms));                                           /* unchanged */
    CHECK(lobby_encode(&h.st, blob, sizeof(blob)) > 0 && lobby_decode(&d, blob, LOBBY_BLOB_MAX) == 0);
    CHECK(d.status.valid && d.status.timer == 1 && d.status.seconds == 431 && d.status.port[0].damage == 287 && d.status.port[0].stocks == 3);
    CHECK(d.status.port[2].kind == 1 && d.status.port[3].kind == 3);
    int short_len = lobby_encode(&h.st, blob, sizeof(blob)) - 20;                   /* a blob without the section: no status */
    CHECK(lobby_decode(&d, blob, short_len) == 0 && !d.status.valid);
    CHECK(lobby_host_match_over(&h) & LOBBY_CHANGED);
    CHECK(!h.st.status.valid && !lobby_host_set_status(&h, &ms));                    /* only during a match */
    CHECK(h.st.phase == LOBBY_CSS && h.st.slot[1].kind == LOBBY_EMPTY && h.st.slot[0].kind == LOBBY_CPU);   /* the late joiner's door stays closed */
    CHECK(!lobby_host_command(&h, 1, (cmd[0] = LCMD_STAGE, cmd), 1));               /* short command */

    /* Handicap: each door carries its own (set by the console that owns it); the host's mode decides whether the match
     * block uses it, and another console's Human door keeps its owner's value. */
    {
        lobby_host k;
        unsigned char blk[LOBBY_BLOCK_SIZE], tm[LOBBY_BLOCK_SIZE];
        lobby_host_init(&k, 0x3);
        lobby_slot a = fighter(LOBBY_HUMAN, 0x14, 0); a.handicap = 2;
        lobby_slot b2 = fighter(LOBBY_HUMAN, 0x02, 1); b2.handicap = 8;
        CHECK(door(&k, 0, 0, a) & LOBBY_CHANGED);
        CHECK(door(&k, 1, 1, b2) & LOBBY_CHANGED);
        CHECK(k.st.slot[0].handicap == 2 && k.st.slot[1].handicap == 8);
        lobby_slot other = b2; other.handicap = 3;                                     /* console 0 edits console 1's door */
        door(&k, 0, 1, other);
        CHECK(k.st.slot[1].handicap == 8);
        lobby_slot cpu = fighter(LOBBY_CPU, 0x0A, 0); cpu.handicap = 9;                /* a CPU's slider: anyone's */
        CHECK(door(&k, 1, 2, cpu) & LOBBY_CHANGED);
        cpu.handicap = 1;
        CHECK(door(&k, 0, 2, cpu) & LOBBY_CHANGED);
        CHECK(k.st.slot[2].handicap == 1);
        lobby_slot bad = cpu; bad.handicap = 12;
        door(&k, 0, 2, bad);
        CHECK(k.st.slot[2].handicap == 0);                                              /* out of range: none */
        door(&k, 0, 2, cpu);
        /* the rules a joiner's character select shows, and the handicap mode, ride in the blob */
        k.st.rules.mode = 0; k.st.rules.time_limit = 3; k.st.rules.stock_count = 2; k.st.rules.stock_time_limit = 6;
        k.st.rules.handicap = 2;
        CHECK(lobby_encode(&k.st, blob, sizeof(blob)) > 0 && lobby_decode(&d, blob, LOBBY_BLOB_MAX) == 0);
        CHECK(!memcmp(&d, &k.st, sizeof(d)));
        CHECK(d.slot[0].handicap == 2 && d.slot[1].handicap == 8 && d.rules.handicap == 2 && d.rules.time_limit == 3);
        /* off: the template's handicap (9) and ratios (1.0) whatever the doors hold */
        k.st.rules.handicap = 0;
        lobby_build_block(tm, &k.st, 0, 2);
        CHECK(tm[0x68] == 9 && tm[0x78] == 0x3F && tm[0x79] == 0x80 && tm[0x7C] == 0x3F && tm[0x7D] == 0x80);
        /* on: each player's handicap and vanilla's ratios (2: 0.4 attack, 2.5 defense; 1: 0.35, 2.857143) */
        k.st.rules.handicap = 1;
        lobby_build_block(blk, &k.st, 0, 2);
        CHECK(blk[0x68] == 2 && blk[0x68 + 0x24] == 8 && blk[0x68 + 0x48] == 1);
        CHECK(blk[0x78] == 0x3E && blk[0x79] == 0xCC && blk[0x7A] == 0xCC && blk[0x7B] == 0xCD);   /* 0.4f */
        CHECK(blk[0x7C] == 0x40 && blk[0x7D] == 0x20 && blk[0x7E] == 0 && blk[0x7F] == 0);           /* 2.5f */
        CHECK(blk[0x80] == 0x3F && blk[0x81] == 0x80);                                  /* model scale untouched */
        CHECK(!memcmp(blk, tm, 0x60) && blk[0x68 + 0x6C] == tm[0x68 + 0x6C]);           /* rules and the empty door */
        /* a door with no handicap yet keeps the template's */
        k.st.slot[1].handicap = 0;
        lobby_build_block(blk, &k.st, 0, 2);
        CHECK(blk[0x68 + 0x24] == 9 && blk[0x78 + 0x24] == 0x3F && blk[0x79 + 0x24] == 0x80);
        /* a console leaving keeps its fighter's handicap for the CPU that plays on */
        k.st.slot[1].handicap = 8;
        lobby_host_sync(&k, 0x1);
        CHECK(k.st.slot[1].kind == LOBBY_CPU && k.st.slot[1].handicap == 8);
    }

    /* The results screen after a match: who played, who is ready, how far a press moved it on, and the stars. */
    {
        lobby_host r;
        lobby_state rd;
        uint8_t rb[LOBBY_BLOB_MAX], c[8];
        lobby_host_init(&r, 0x7);                                                       /* consoles 0, 1, 2 */
        lobby_slot p0 = fighter(LOBBY_HUMAN, 0x14, 0); memcpy(p0.tag, "TAGZERO\0", 8);
        lobby_slot p1 = fighter(LOBBY_HUMAN, 0x02, 0); memcpy(p1.tag, "TAGONE\0\0", 8);
        lobby_slot p2 = fighter(LOBBY_HUMAN, 0x0A, 0);
        CHECK(lobby_host_set_door(&r, 0, &p0) && door(&r, 1, 1, p1) && door(&r, 2, 2, p2));
        CHECK(!lobby_host_results_begin(&r) && r.st.phase == LOBBY_CSS);                /* not from the CSS */
        lobby_host_match_began(&r, 0);
        CHECK(r.st.played == 0x7 && r.st.ready == 0 && r.st.advance == 0);
        CHECK(!lobby_host_results(&r, 1, 1, 2));                                         /* not before the results */
        CHECK(lobby_host_sync(&r, 0xF) == 0);                                            /* console 3 joins during the match */
        CHECK(lobby_host_results_begin(&r) & LOBBY_CHANGED);
        CHECK(r.st.phase == LOBBY_RESULTS && r.st.ready == 0 && r.st.advance == 0);
        CHECK(!lobby_host_results(&r, 3, 1, 2) && r.st.ready == 0 && r.st.advance == 0); /* it did not play */
        CHECK(!lobby_host_results(&r, 7, 1, 2));
        CHECK(lobby_host_results(&r, 1, 0, 2) & LOBBY_CHANGED);                          /* past the intro */
        CHECK(r.st.advance == 2 && r.st.ready == 0);
        CHECK(!lobby_host_results(&r, 2, 0, 1) && r.st.advance == 2);                    /* never back */
        CHECK(!lobby_host_command(&r, 2, c, lobby_cmd_results(c, 0, 0)) && r.st.advance == 2);
        CHECK(lobby_host_command(&r, 2, c, lobby_cmd_results(c, 1, 0)) & LOBBY_CHANGED);
        CHECK(r.st.ready == 0x4);
        CHECK(lobby_host_command(&r, 2, c, lobby_cmd_results(c, 0, 0)) && r.st.ready == 0);   /* ready toggles back */
        CHECK(lobby_host_command(&r, 2, c, lobby_cmd_results(c, 1, 9)) && r.st.ready == 0x4 && r.st.advance == 2);
        CHECK(!lobby_host_command(&r, 2, c, 2));                                         /* too short */
        CHECK(lobby_host_results(&r, 0, 1, 0) && r.st.ready == 0x5);
        CHECK(!door(&r, 1, 1, fighter(LOBBY_HUMAN, 0x03, 0)) && r.st.slot[1].fighter == 0x02);   /* doors frozen */
        /* the results section rides in the blob */
        r.st.stars[2] = 7;
        CHECK(lobby_encode(&r.st, rb, sizeof(rb)) > 0 && lobby_decode(&rd, rb, LOBBY_BLOB_MAX) == 0);
        CHECK(!memcmp(&rd, &r.st, sizeof(rd)) && rd.phase == LOBBY_RESULTS && rd.played == 0x7 && rd.ready == 0x5);
        CHECK(rd.advance == 2 && rd.stars[2] == 7);
        {   /* an older blob without it: all zero */
            int full = lobby_encode(&r.st, rb, sizeof(rb));
            CHECK(lobby_decode(&rd, rb, full - (3 + LOBBY_SLOTS)) == 0);
            CHECK(rd.played == 0 && rd.ready == 0 && rd.advance == 0 && !rd.stars[2] && rd.status.valid == r.st.status.valid);
            CHECK(lobby_encode(&r.st, rb, full - 1) < full);                             /* no room: the section is left out */
        }
        r.st.stars[2] = 0;
        /* console 1 leaves during the results: it counts as ready, its door waits for the results' end */
        CHECK(lobby_host_sync(&r, 0xD) & LOBBY_CHANGED);
        CHECK(r.st.ready == 0x7 && r.st.slot[1].kind == LOBBY_HUMAN && r.st.slot[3].kind == LOBBY_EMPTY);
        {
            uint8_t st[4] = { 1, 0, 2, 0 }, hc[4] = { 4, 6, 0, 3 };
            CHECK(lobby_host_results_over(&r, st, hc) & LOBBY_CHANGED);
            CHECK(r.st.phase == LOBBY_CSS && r.st.played == 0 && r.st.ready == 0 && r.st.advance == 0);
            CHECK(r.st.stars[0] == 1 && r.st.stars[2] == 2);
            CHECK(r.st.slot[0].handicap == 4 && r.st.slot[2].handicap == 0);             /* 0: keep */
            CHECK(r.st.slot[3].kind == LOBBY_EMPTY && r.st.slot[3].handicap == 0);       /* not on an empty door */
            CHECK(r.st.slot[1].kind == LOBBY_CPU && r.st.slot[1].handicap == 6);         /* the leave, applied now */
            CHECK(!lobby_host_results_over(&r, st, hc));                                 /* only once */
        }
        /* stars: kept when a console leaves and comes back with the same tag, cleared on a tag change */
        CHECK(lobby_host_sync(&r, 0xF) == 0);                                            /* console 1 back: the door stays a CPU */
        r.st.stars[1] = 3;
        CHECK(door(&r, 1, 1, p1) && r.st.slot[1].kind == LOBBY_HUMAN && r.st.stars[1] == 3);
        p0 = r.st.slot[0];
        CHECK(lobby_host_set_door(&r, 0, &p0) == 0 && r.st.stars[0] == 1);              /* the same door again */
        p0.color = 3;
        CHECK(lobby_host_set_door(&r, 0, &p0) && r.st.stars[0] == 1);                    /* a costume: kept */
        lobby_slot untagged = p0; memset(untagged.tag, 0, 8);
        CHECK(lobby_host_set_door(&r, 0, &untagged) && r.st.stars[0] == 0);              /* the tag removed: cleared */
        r.st.stars[0] = 2;
        CHECK(lobby_host_set_door(&r, 0, &untagged) == 0 && r.st.stars[0] == 2);         /* same (no) tag: kept */
        lobby_slot cpu0 = fighter(LOBBY_CPU, 0x14, 0);
        CHECK(lobby_host_set_door(&r, 0, &cpu0) && r.st.stars[0] == 2);                  /* a CPU in between: kept */
        CHECK(lobby_host_set_door(&r, 0, &p0) && r.st.stars[0] == 0);                    /* back Human, another tag */
        /* the first Human tag a port shows does not clear anything */
        r.st.stars[3] = 4;
        CHECK(door(&r, 3, 3, fighter(LOBBY_HUMAN, 0x05, 0)) && r.st.stars[3] == 4);
        /* a new lobby has none */
        lobby_host_init(&r, 0x3);
        CHECK(!r.st.stars[0] && !r.st.stars[1] && !r.st.stars[2] && !r.st.stars[3]);

        /* a sudden death: a follow-up game with the tied game's consoles; joins waiting from the tied game stay */
        lobby_host_init(&r, 0x3);
        CHECK(lobby_host_set_door(&r, 0, &p0) && door(&r, 1, 1, p1));
        lobby_host_match_began(&r, 0);
        CHECK(lobby_host_sync(&r, 0x7) == 0);                                            /* console 2 joins in the tied game */
        lobby_host_match_began(&r, 1);
        CHECK(r.st.played == 0x3 && r.st.phase == LOBBY_MATCH && (r.joined_in_match & 0x4));
        CHECK(lobby_host_sync(&r, 0x5) == LOBBY_CHANGED && r.st.gone == 0x2);            /* console 1 leaves in the sudden death
                                                                                          * (its name is kept: a change) */
        lobby_host_match_began(&r, 1);                                                   /* (another tie) */
        CHECK(r.st.played == 0x1);
        CHECK(lobby_host_results_begin(&r) && r.st.ready == 0x0);                         /* console 1 left: not played any more */
        CHECK(lobby_host_results(&r, 0, 1, 2) && r.st.ready == 0x1);
        CHECK(lobby_host_results_over(&r, NULL, NULL) && r.st.phase == LOBBY_CSS);
        CHECK(r.st.slot[1].kind == LOBBY_CPU && r.st.slot[2].kind == LOBBY_EMPTY && r.present == 0x5);

        /* Another console takes the port of a player who left during the match (a rejoin). It did not play:
         * its port counts as ready on the results screen and its presses there are not the player's. */
        lobby_host_init(&r, 0x3);
        CHECK(lobby_host_set_door(&r, 0, &p0) && door(&r, 1, 1, p1));
        lobby_host_match_began(&r, 0);
        CHECK(r.st.played == 0x3);
        CHECK(lobby_host_sync(&r, 0x1) == LOBBY_CHANGED && lobby_host_sync(&r, 0x3) == 0);   /* port 1 drops (its name is kept),
                                                                                              * a newcomer takes it */
        CHECK(r.left_played == 0x2);
        CHECK(lobby_host_results_begin(&r) && r.st.ready == 0x2);                         /* the player who left counts as ready */
        CHECK(!lobby_host_results(&r, 1, 0, 2) && r.st.ready == 0x2);                    /* the newcomer cannot unready it */
        CHECK(lobby_host_results(&r, 0, 1, 2) && r.st.ready == 0x3);
        CHECK(lobby_host_results_over(&r, NULL, NULL) && r.left_played == 0);
        /* and it does not inherit the door: the leave is applied (Human -> CPU) before its arrival */
        CHECK(r.st.slot[1].kind == LOBBY_CPU && r.st.slot[1].tag[0] == 0 && r.present == 0x3);
        /* the same during the results: a leave and a rejoin before they end */
        CHECK(door(&r, 1, 1, p1) && r.st.slot[1].kind == LOBBY_HUMAN);
        lobby_host_match_began(&r, 0);
        CHECK(lobby_host_results_begin(&r) && r.st.ready == 0);
        CHECK(lobby_host_sync(&r, 0x1) & LOBBY_CHANGED);                                  /* leaves on the results: ready */
        CHECK(r.st.ready == 0x2 && lobby_host_sync(&r, 0x3) == 0);
        CHECK(!lobby_host_results(&r, 1, 0, 2) && r.st.ready == 0x2);
        CHECK(lobby_host_results(&r, 0, 1, 2) && lobby_host_results_over(&r, NULL, NULL));
        CHECK(r.st.slot[1].kind == LOBBY_CPU);
    }

    /* Doors in use and edits from an older view. Consoles on ports 0..2; door 2 is a CPU. */
    {
        lobby_host u;
        lobby_state x;
        uint8_t b[LOBBY_BLOB_MAX];
        lobby_slot cpu = fighter(LOBBY_CPU, 0x02, 0), closed = fighter(LOBBY_EMPTY, 0, 0), lvl = cpu;
        lvl.level = 8;
        lobby_host_init(&u, 0x7);
        CHECK(door(&u, 0, 0, fighter(LOBBY_HUMAN, 0x14, 0)) && door(&u, 1, 1, fighter(LOBBY_HUMAN, 0x02, 1)));
        CHECK(door(&u, 0, 2, cpu) && u.st.slot[2].kind == LOBBY_CPU);
        /* compare-and-set: a level edit made from a view where door 2 was a CPU, after another console closed it */
        CHECK(door_base(&u, 0, 2, closed, LOBBY_CPU + 1) && u.st.slot[2].kind == LOBBY_EMPTY);
        CHECK(!door_base(&u, 1, 2, lvl, LOBBY_CPU + 1) && u.st.slot[2].kind == LOBBY_EMPTY);   /* stale: dropped */
        CHECK(door_base(&u, 1, 2, cpu, LOBBY_EMPTY + 1) && u.st.slot[2].kind == LOBBY_CPU);    /* a switch with a stale base: applied */
        CHECK(door_base(&u, 0, 2, closed, LOBBY_HUMAN + 1) && u.st.slot[2].kind == LOBBY_EMPTY); /* (the sender switched it) */
        CHECK(door_base(&u, 1, 2, lvl, 0) && u.st.slot[2].kind == LOBBY_CPU);                  /* base 0: as before */
        CHECK(door_base(&u, 1, 2, lvl, LOBBY_CPU + 1) == 0);                                    /* current base, no change */
        lvl.level = 9;
        CHECK(door_base(&u, 1, 2, lvl, LOBBY_CPU + 1) && u.st.slot[2].level == 9);              /* current base, a change */
        /* in use: console 1 holds door 2's level slider */
        CHECK(busy_cmd(&u, 1, 1, 0x4) & LOBBY_CHANGED);
        CHECK(u.st.in_use == 0x4 && u.st.busy == 0x2);
        CHECK(!door_base(&u, 0, 2, closed, LOBBY_CPU + 1) && u.st.slot[2].kind == LOBBY_CPU);   /* another console: refused */
        CHECK(!door_base(&u, 2, 2, closed, 0) && u.st.slot[2].kind == LOBBY_CPU);
        CHECK(!lobby_host_edit_door(&u, 0, 2, &closed, 0) && u.st.slot[2].kind == LOBBY_CPU);  /* the host's own console too */
        lvl.level = 4;
        CHECK(door_base(&u, 1, 2, lvl, LOBBY_CPU + 1) && u.st.slot[2].level == 4);              /* its user drags on */
        CHECK(door_base(&u, 2, 0, fighter(LOBBY_CPU, 0x14, 0), 0) && u.st.slot[0].kind == LOBBY_CPU);   /* other doors: free */
        CHECK(door_base(&u, 1, 2, closed, LOBBY_CPU + 1) && u.st.slot[2].kind == LOBBY_EMPTY);   /* its user may switch it */
        CHECK(door_base(&u, 1, 2, cpu, LOBBY_EMPTY + 1) && u.st.slot[2].kind == LOBBY_CPU);
        /* the doors in use go to every console (the results byte's high nibble) */
        int n = lobby_encode(&u.st, b, sizeof(b));
        CHECK(n > 0 && n <= LOBBY_BLOB_MAX && lobby_decode(&x, b, n) == 0 && x.in_use == 0x4 && x.played == 0);
        u.st.played = 0x5;
        n = lobby_encode(&u.st, b, sizeof(b));
        CHECK(lobby_decode(&x, b, n) == 0 && x.in_use == 0x4 && x.played == 0x5);
        u.st.played = 0;
        /* let go: free again */
        CHECK(busy_cmd(&u, 1, 0, 0) & LOBBY_CHANGED);
        CHECK(u.st.in_use == 0 && door_base(&u, 0, 2, closed, LOBBY_CPU + 1) && u.st.slot[2].kind == LOBBY_EMPTY);
        /* two consoles' doors at once; the OR is published, and a console that leaves releases its own */
        CHECK(busy_cmd(&u, 1, 1, 0x4) && busy_cmd(&u, 2, 1, 0x1) && u.st.in_use == 0x5);
        CHECK(lobby_host_sync(&u, 0x5) & LOBBY_CHANGED);                                         /* console 1 leaves */
        CHECK(u.st.in_use == 0x1 && u.in_use_by[1] == 0 && !(u.st.busy & 0x2));
        CHECK(door_base(&u, 0, 2, cpu, LOBBY_EMPTY + 1) && u.st.slot[2].kind == LOBBY_CPU);
        CHECK(!door_base(&u, 0, 0, closed, LOBBY_CPU + 1));                                      /* console 2 still uses door 0 */
        /* an older console's two-byte busy command clears the doors it might have had */
        uint8_t old[2] = {LCMD_BUSY, 1};
        CHECK(lobby_host_command(&u, 2, old, 2) && u.st.in_use == 0);
    }

    /* The match status carries each port's live fighter (Zelda <-> Sheik), and the blob still fits */
    {
        lobby_host u;
        lobby_state x;
        uint8_t b[LOBBY_BLOB_MAX];
        lobby_status st;
        lobby_host_init(&u, 0x3);
        lobby_host_match_began(&u, 0);
        memset(&st, 0, sizeof(st));
        st.timer = 1; st.seconds = 90;
        st.port[0].kind = 0; st.port[0].stocks = 3; st.port[0].damage = 42; st.port[0].fighter = 18; st.port[0].costume = 2;
        st.port[1].kind = 1; st.port[1].stocks = 4; st.port[1].fighter = 2; st.port[1].costume = 1;
        st.port[2].kind = st.port[3].kind = 3;
        CHECK(lobby_host_set_status(&u, &st) & LOBBY_CHANGED);
        CHECK(lobby_host_set_status(&u, &st) == 0);
        st.port[0].fighter = 19;                                                          /* Zelda transforms */
        CHECK(lobby_host_set_status(&u, &st) & LOBBY_CHANGED);
        u.st.in_use = 0xF; u.st.busy = 0xF; u.st.played = 0xF; u.st.ready = 0xF; u.st.advance = 2;
        for (int i = 0; i < 4; ++i) { u.st.slot[i] = fighter(LOBBY_HUMAN, 0x19, 5); memset(u.st.slot[i].tag, 0x82, 8); u.st.stars[i] = 99; }
        int n = lobby_encode(&u.st, b, sizeof(b));
        CHECK(n > 0 && n <= LOBBY_BLOB_MAX);
        printf("lobby_unit: the lobby blob is %d of %d bytes\n", n, LOBBY_BLOB_MAX);
        CHECK(lobby_decode(&x, b, n) == 0 && x.status.valid && x.status.port[0].fighter == 19 && x.status.port[0].costume == 2 &&
              x.status.port[1].fighter == 2 && x.status.port[1].costume == 1 && x.status.port[0].damage == 42 &&
              x.status.seconds == 90 && x.in_use == 0xF && x.played == 0xF && x.stars[3] == 99);
        b[0] = 0x4C;                                                                      /* the blob of an older build */
        CHECK(lobby_decode(&x, b, n) == -1);
    }

    /* Why a console is busy */
    {
        lobby_host u;
        lobby_state x;
        uint8_t b[LOBBY_BLOB_MAX];
        lobby_host_init(&u, 0xF);
        for (int code = 0; code <= LOBBY_BUSY_MAX; ++code) {
            busy_cmd(&u, 2, code, 0);
            CHECK(u.st.busy_why[2] == code && ((u.st.busy >> 2) & 1) == (code != 0));
            int n = lobby_encode(&u.st, b, sizeof(b));
            CHECK(lobby_decode(&x, b, n) == 0 && x.busy_why[2] == code && ((x.busy >> 2) & 1) == (code != 0));
        }
        CHECK(busy_cmd(&u, 1, 9, 0) && u.st.busy_why[1] == LOBBY_BUSY_SLIDER && (u.st.busy & 0x2));   /* unknown: 1 */
        CHECK(busy_cmd(&u, 3, LOBBY_BUSY_TAGS, 0) && busy_cmd(&u, 3, LOBBY_BUSY_NAMING, 0) && u.st.busy_why[3] == LOBBY_BUSY_NAMING);
        CHECK(!lobby_can_start(&u.st));                                                   /* busy: no Start, as before */
        CHECK(lobby_host_sync(&u, 0x7) & LOBBY_CHANGED);                                   /* port 3 leaves */
        CHECK(u.st.busy_why[3] == 0 && !(u.st.busy & 0x8) && u.st.busy_why[1] == LOBBY_BUSY_SLIDER);
        CHECK(busy_cmd(&u, 1, 0, 0) && !(u.st.busy & 0x2) && u.st.busy_why[1] == 0);
        uint8_t old[2] = {LCMD_BUSY, 1};                                                  /* an older console's command */
        CHECK(lobby_host_command(&u, 2, old, 2) && u.st.busy_why[2] == LOBBY_BUSY_SLIDER);
    }

    /* A console that leaves the running match keeps its name in the lobby (gone), and one that
     * takes its port meanwhile did not play */
    {
        lobby_host u;
        lobby_state x;
        uint8_t b[LOBBY_BLOB_MAX];
        lobby_host_init(&u, 0x7);
        lobby_host_match_began(&u, 0);
        lobby_host_name(&u, 0, "Alice");
        lobby_host_name(&u, 1, "\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86");
        lobby_host_name(&u, 2, "Carol");
        CHECK(u.st.gone == 0);
        CHECK(lobby_host_sync(&u, 0x5) & LOBBY_CHANGED);                                  /* port 1 leaves mid-match */
        CHECK(u.st.gone == 0x2 && !strcmp(u.st.gone_name[1], u.played_name[1]));
        lobby_host_sync(&u, 0x7);                                                         /* another console takes port 1 */
        CHECK(u.st.gone == 0x2);
        CHECK(lobby_host_sync(&u, 0x3) & LOBBY_CHANGED);                                  /* port 2 leaves too */
        CHECK(u.st.gone == 0x6);
        int n = lobby_encode(&u.st, b, sizeof(b));
        CHECK(n > 0 && n <= LOBBY_BLOB_MAX && lobby_decode(&x, b, n) == 0);
        CHECK(x.gone == 0x6 && !strcmp(x.gone_name[1], u.played_name[1]) && !strcmp(x.gone_name[2], "Carol"));
        CHECK(lobby_decode(&x, b, n - 3) == 0 && x.gone == 0x2);                          /* cut short: the names that came */
        /* the largest lobby, with three long names that left: still within the blob, names that do not fit are left out */
        u.st.played = 0xF; u.st.in_use = 0xF; u.st.busy = 0xF;
        for (int i = 0; i < 4; ++i) { u.st.slot[i] = fighter(LOBBY_HUMAN, 0x19, 5); memset(u.st.slot[i].tag, 0x82, 8); u.st.busy_why[i] = 4; }
        u.st.status.valid = 1;
        for (int i = 1; i < 4; ++i) { u.st.gone |= (uint8_t)(1u << i); memset(u.st.gone_name[i], 'W', 30); u.st.gone_name[i][30] = 0; }
        n = lobby_encode(&u.st, b, sizeof(b));
        CHECK(n > 0 && n <= LOBBY_BLOB_MAX && lobby_decode(&x, b, n) == 0 && x.gone == 0x6 && strlen(x.gone_name[2]) == 30);
        printf("lobby_unit: the largest blob is %d of %d bytes\n", n, LOBBY_BLOB_MAX);
        lobby_host_results_begin(&u);
        lobby_host_results_over(&u, NULL, NULL);
        CHECK(u.st.gone == 0 && !u.st.gone_name[1][0]);                                  /* the lobby reopened */
    }

    /* The beacon, with and without the host's Mii */
    {
        lobby_host u;
        lobby_beacon bb;
        uint8_t b[LOBBY_BEACON_MAX + 8], mii[LOBBY_MII_BYTES], got[LOBBY_MII_BYTES];
        int has;
        for (int i = 0; i < LOBBY_MII_BYTES; ++i) mii[i] = (uint8_t)(i * 7 + 3);
        CHECK(!lobby_mii_ok(mii));
        lobby_mii_seal(mii);
        CHECK(lobby_mii_ok(mii));
        uint8_t zero[LOBBY_MII_BYTES];
        memset(zero, 0, sizeof(zero));
        CHECK(!lobby_mii_ok(zero));                                                       /* all zero: no Mii, though its CRC is 0 */
        lobby_host_init(&u, 0x3);
        u.st.rules.teams = 1;
        u.st.slot[0] = fighter(LOBBY_HUMAN, 18, 2); u.st.slot[0].team = 2;
        u.st.slot[1] = fighter(LOBBY_CPU, 9, 1); u.st.slot[1].team = 1;
        u.st.slot[2].kind = LOBBY_HUMAN; u.st.slot[2].chosen = 0;
        u.st.slot[3].kind = LOBBY_EMPTY;
        int n = lobby_encode_beacon(&u.st, NULL, b, sizeof(b));
        CHECK(n == 20);
        CHECK(lobby_decode_beacon(&bb, got, &has, b, n) == 0 && !has && bb.teams == 1 && bb.phase == LOBBY_CSS);
        CHECK(bb.slot[0].kind == LOBBY_HUMAN && bb.slot[0].fighter == 18 && bb.slot[0].costume == 2 && bb.slot[0].team == 2 && bb.slot[0].chosen == 1);
        CHECK(bb.slot[1].kind == LOBBY_CPU && bb.slot[1].team == 1 && bb.slot[2].chosen == 0 && bb.slot[3].kind == LOBBY_EMPTY);
        n = lobby_encode_beacon(&u.st, mii, b, sizeof(b));
        CHECK(n == 116 && n <= 200);
        CHECK(lobby_decode_beacon(&bb, got, &has, b, n) == 0 && has && !memcmp(got, mii, sizeof(mii)));
        b[20 + 0x20] ^= 0x5A;                                                             /* test_mii_corrupt: rejected */
        CHECK(lobby_decode_beacon(&bb, got, &has, b, n) == 0 && !has);
        b[20 + 0x20] ^= 0x5A;
        b[n - 1] ^= 1;                                                                    /* a flipped CRC byte: no Mii */
        CHECK(lobby_decode_beacon(&bb, got, &has, b, n) == 0 && !has);
        CHECK(lobby_decode_beacon(&bb, got, &has, b, n - 10) == 0 && !has);              /* cut short */
        CHECK(lobby_encode_beacon(&u.st, mii, b, 100) == 0);
        uint8_t blob[LOBBY_BLOB_MAX];
        CHECK(lobby_decode_beacon(&bb, got, &has, blob, lobby_encode(&u.st, blob, sizeof(blob))) == -1);   /* a lobby blob is not a beacon */
        /* during a match: the live fighter and a dropped console's CPU */
        lobby_host_match_began(&u, 0);
        lobby_status st;
        memset(&st, 0, sizeof(st));
        st.port[0].kind = 0; st.port[0].fighter = 19; st.port[0].costume = 2;
        st.port[1].kind = 1; st.port[1].fighter = 9;
        st.port[2].kind = 1; st.port[2].fighter = 4;
        st.port[3].kind = 3;
        lobby_host_set_status(&u, &st);
        n = lobby_encode_beacon(&u.st, mii, b, sizeof(b));
        CHECK(lobby_decode_beacon(&bb, got, &has, b, n) == 0 && bb.phase == LOBBY_MATCH && bb.slot[0].fighter == 19);
        CHECK(bb.slot[2].kind == LOBBY_CPU && bb.slot[2].fighter == 4 && bb.slot[2].chosen == 1 && bb.slot[3].kind == LOBBY_EMPTY);
        /* every lobby state stays within a beacon's 200 bytes */
        for (int phase = LOBBY_CSS; phase <= LOBBY_RESULTS; ++phase) {
            u.st.phase = (uint8_t)phase;
            CHECK(lobby_encode_beacon(&u.st, mii, b, sizeof(b)) <= 200);
        }
    }

    /* the Stage Selection rule: the host decides who picks, or the stage */
    {
        lobby_host u;
        lobby_state x;
        uint8_t b[LOBBY_BLOB_MAX], c[24];
        lobby_host_init(&u, 0x7);                                     /* consoles on ports 0, 1, 2 */
        u.st.slot[0] = fighter(LOBBY_HUMAN, 2, 0);
        u.st.slot[1] = fighter(LOBBY_CPU, 9, 0);                       /* a CPU: never the picker */
        u.st.slot[2] = fighter(LOBBY_HUMAN, 4, 0);
        u.st.slot[3] = fighter(LOBBY_HUMAN, 5, 0);                     /* no console on port 3 */
        /* Any: every console picks; the rule and picker survive the wire */
        CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == LOBBY_PICK_ANY);
        CHECK(lobby_decode(&x, b, lobby_encode(&u.st, b, sizeof(b))) == 0 && x.sss_picker == LOBBY_PICK_ANY);
        CHECK(lobby_host_pick_stage(&u, 2, 0x20, 0) & LOBBY_START_MATCH);
        /* Turns: round the Human ports with a console (0, 2, 0, ...), skipping the CPU and the empty port */
        u.st.phase = LOBBY_CSS;
        u.st.rules.stage_sel = LOBBY_SSS_TURNS;
        int want[] = {0, 2, 0, 2};
        for (int k = 0; k < 4; ++k) {
            u.st.phase = LOBBY_CSS;
            CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == want[k]);
            CHECK(lobby_decode(&x, b, lobby_encode(&u.st, b, sizeof(b))) == 0 && x.sss_picker == want[k] &&
                  x.rules.stage_sel == LOBBY_SSS_TURNS);
        }
        /* only the picker picks, and only the picker goes back */
        int other = u.st.sss_picker == 0 ? 2 : 0, picker = u.st.sss_picker;
        CHECK(!lobby_host_command(&u, other, c, lobby_cmd_stage(c, 0x20, 0)) && !u.st.stage_picked);
        CHECK(!lobby_host_command(&u, other, c, lobby_cmd_back(c)) && u.st.phase == LOBBY_STAGE);
        CHECK(lobby_host_command(&u, picker, c, lobby_cmd_back(c)) && u.st.phase == LOBBY_CSS);
        /* L+R+Start (the soft reset) goes back from any console, the picker or not; never after the pick */
        CHECK(lobby_host_start(&u, 0));
        other = u.st.sss_picker == 0 ? 2 : 0;
        CHECK(lobby_host_command(&u, other, c, lobby_cmd_back_reset(c)) && u.st.phase == LOBBY_CSS);
        CHECK(lobby_host_start(&u, 0));
        CHECK(lobby_host_pick_stage(&u, u.st.sss_picker, 0x1F, 0));
        CHECK(!lobby_host_command(&u, other, c, lobby_cmd_back_reset(c)));
        u.st.phase = LOBBY_CSS;
        u.st.stage_picked = 0;
        u.st.rules.stage_sel = LOBBY_SSS_TURNS;
        /* the picker leaving passes the pick to the next Human port with a console; a pick from it is refused */
        u.st.rules.stage_sel = LOBBY_SSS_LOSER;
        lobby_host_set_loser(&u, 2);
        CHECK(lobby_host_start(&u, 2) && u.st.sss_picker == 2);      /* port 2 started: its leaving would end it */
        u.st.phase = LOBBY_CSS;
        CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == 2);      /* Loser: the recorded loser */
        CHECK(lobby_host_sync(&u, 0x3) & LOBBY_CHANGED);               /* port 2 leaves */
        CHECK(u.st.phase == LOBBY_STAGE && u.st.sss_picker == 0);
        CHECK(!lobby_host_command(&u, 2, c, lobby_cmd_stage(c, 0x20, 0)));
        CHECK(lobby_host_pick_stage(&u, 0, 0x1F, 0) & LOBBY_START_MATCH);
        /* Loser with the loser gone before Start: the next Human port with a console from it */
        u.st.phase = LOBBY_CSS;
        lobby_host_set_loser(&u, 3);
        CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == 0);
        /* Loser, first match: a random Human port with a console, never the CPU or an empty port */
        for (int k = 0; k < 16; ++k) {
            u.st.phase = LOBBY_CSS;
            lobby_host_set_loser(&u, -1);
            CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == 0);   /* only port 0 is Human with a console now */
        }
        /* Random and Ordered: the host (port 0) picks from its save at once */
        u.st.phase = LOBBY_CSS;
        u.st.rules.stage_sel = LOBBY_SSS_RANDOM;
        CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == 0);
        /* a random pick: taken, starts nothing; then the host's console names the stage and the match starts */
        u.st.rules.stage_sel = LOBBY_SSS_ANY;
        u.st.phase = LOBBY_CSS;
        lobby_host_sync(&u, 0x7);
        CHECK(lobby_host_start(&u, 0) && u.st.sss_picker == LOBBY_PICK_ANY);
        int r = lobby_host_command(&u, 1, c, lobby_cmd_stage(c, LOBBY_STAGE_RANDOM, 0));
        CHECK((r & LOBBY_CHANGED) && !(r & LOBBY_START_MATCH) && u.st.stage == LOBBY_STAGE_RANDOM);
        CHECK(!lobby_host_command(&u, 2, c, lobby_cmd_stage(c, 0x20, 0)));          /* the first pick still wins */
        CHECK(!lobby_host_command(&u, 1, c, lobby_cmd_stage(c, 0x20, 0)));          /* only the host resolves it */
        CHECK(!lobby_host_command(&u, 1, c, lobby_cmd_back(c)) && u.st.phase == LOBBY_STAGE);
        CHECK((lobby_host_pick_stage(&u, 0, 0x1F, 0) & LOBBY_START_MATCH) && u.st.stage == 0x1F);
        CHECK(!lobby_host_pick_stage(&u, 0, 0x20, 0));                              /* and only once */
        /* a joiner's own rule plays no part: an unknown rule on the wire reads as Any */
        u.st.rules.stage_sel = 9;
        CHECK(lobby_decode(&x, b, lobby_encode(&u.st, b, sizeof(b))) == 0 && x.rules.stage_sel == LOBBY_SSS_ANY);
    }

    printf("lobby_unit: ok\n");
    return 0;
}
