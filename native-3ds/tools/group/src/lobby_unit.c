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

static int door(lobby_host *h, int from, int slot, lobby_slot s)
{
    uint8_t c[LOBBY_BLOB_MAX];
    int n = lobby_cmd_door(c, slot, &s);
    return lobby_host_command(h, from, c, n);
}

int main(void)
{
    lobby_host h;
    uint8_t blob[LOBBY_BLOB_MAX], cmd[24];
    lobby_state d;

    /* host alone: door 0 Human, the rest empty; cannot start */
    lobby_host_init(&h, 1);
    CHECK(h.st.slot[0].kind == LOBBY_HUMAN && h.st.slot[1].kind == LOBBY_EMPTY);
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
    cmd[0] = LCMD_DOOR; cmd[1] = 9; memset(cmd + 2, 0xEE, 14);
    CHECK(!lobby_host_command(&h, 0, cmd, 16));
    cmd[1] = 1;
    lobby_host_command(&h, 0, cmd, 16);
    CHECK(h.st.slot[1].kind == LOBBY_EMPTY && h.st.slot[1].fighter == 0);
    CHECK(!lobby_host_command(&h, 7, cmd, 16));

    /* a console joins port 2 (index 1): the empty door becomes Human with no fighter; a CPU door stays */
    CHECK(lobby_host_sync(&h, 0x3) & LOBBY_CHANGED);
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

    /* ...and in a match: joins and leaves wait for the end */
    h.st.slot[1] = fighter(LOBBY_EMPTY, 0, 0); h.st.slot[1].chosen = 0;
    h.st.slot[3] = fighter(LOBBY_CPU, 4, 0);
    lobby_host_match_began(&h);
    CHECK(h.st.phase == LOBBY_MATCH);
    CHECK(lobby_host_sync(&h, 0x7) == 0 && h.st.slot[1].kind == LOBBY_EMPTY);       /* late joiner: not touched yet */
    CHECK(lobby_host_sync(&h, 0x6) == 0 && h.st.slot[0].kind == LOBBY_HUMAN);       /* host? slot 0 left: still Human during the match */
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
    CHECK(h.st.phase == LOBBY_CSS && h.st.slot[1].kind == LOBBY_HUMAN && h.st.slot[0].kind == LOBBY_CPU);
    CHECK(!lobby_host_command(&h, 1, (cmd[0] = LCMD_STAGE, cmd), 1));               /* short command */

    printf("lobby_unit: ok\n");
    return 0;
}
