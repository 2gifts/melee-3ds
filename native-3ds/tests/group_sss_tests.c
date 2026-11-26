/* Host tests for group play's stage select rules (port/engine/group/sss_rules.c): who has a cursor, Random from a
 * console that is not the host, the reaction to another console's pick (shown, locked here, unknown here), a hidden
 * pick's end, Back and a lost session. The host's own arbitration (first pick wins, Turns, Loser, the picker passing
 * on) is tests/group_lobby_tests.c. Run by tools/test_group.py. */
#include <group_rules.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("group_sss: FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static GrSssIn idle(void)
{
    GrSssIn in;
    memset(&in, 0, sizeof in);
    in.hidden = -1;
    in.phase = 1;
    return in;
}

int main(void)
{
    GrSssIn in;
    GrSssOut out;
    int stkind[GR_SSS_ICONS], unlocked[GR_SSS_ICONS], i;

    /* the picker: a port, or any console (0xFF in the lobby) */
    CHECK(gr_sss_picker(2) == 2 && gr_sss_picker(0xFF) == -1 && gr_sss_picker(4) == -1);
    CHECK(gr_sss_unk_stage(2) == 3 && gr_sss_unk_stage(0xFF) == 0);
    /* Turns / Loser: only the picker's console has a cursor; Any: everyone */
    CHECK(gr_sss_no_cursor(1, 2, 0) && !gr_sss_no_cursor(1, 2, 2) && !gr_sss_no_cursor(1, -1, 0) &&
          !gr_sss_no_cursor(0, 2, 0));
    /* the host's Random and Ordered: vanilla's stage select on the host (its save picks at once) */
    CHECK(gr_sss_host_picks(1, 1) && gr_sss_host_picks(1, 2) && !gr_sss_host_picks(1, 3) && !gr_sss_host_picks(0, 1));
    /* Random on a console that is not the host goes to the host */
    CHECK(gr_sss_random_to_host(1, 1, 0) && !gr_sss_random_to_host(1, 1, 1) && !gr_sss_random_to_host(0, 1, 0) &&
          !gr_sss_random_to_host(1, 0, 0));
    /* leaving with a pick: sent unless already sent or another console's */
    CHECK(gr_sss_exit_sends(0, 0) && !gr_sss_exit_sends(1, 0) && !gr_sss_exit_sends(0, 1));

    /* nothing happening */
    in = idle();
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_NONE && !out.remote);
    /* a lost session wins over everything: back to the character select, which leaves */
    in.lost = 1;
    in.hidden = 5;
    in.stage_picked = 1;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_LOST);
    /* our pick animates: sent once */
    in = idle();
    in.loading = 1;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_SEND);
    in.sent = 1;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_NONE);
    in.sent = 0;
    in.remote = 1;                                    /* the animation shows another console's pick: not sent */
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_NONE);
    /* another console picked stage 0x10: shown here, the pick is remote */
    in = idle();
    in.stage_picked = 1;
    in.stage = 0x10;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_SHOW && out.stage == 0x10 && out.remote);
    /* the match already started on the host (phase 2) without a pick in this view: the same */
    in.stage_picked = 0;
    in.phase = 2;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_SHOW);
    /* Random from a joiner, seen by the host: resolved once from the host's save */
    in = idle();
    in.stage_picked = 1;
    in.stage = MP_GROUP_STAGE_RANDOM;
    in.host = 1;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_RESOLVE && out.remote);
    in.resolving = 1;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_NONE && out.remote);
    /* ... seen by another joiner: loads without a stage until the host's pick comes */
    in.host = 0;
    in.resolving = 0;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_HIDDEN && out.stage == GR_SSS_FROM_LOBBY);
    /* a hidden pick waits for its "Now loading", then leaves: with its own stage (locked here) ... */
    in = idle();
    in.hidden = 0x18;
    in.loading = 1;
    in.timer = 20;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_NONE);
    in.loading = 2;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_NONE);    /* the timer is still running */
    in.timer = 0;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_LEAVE && out.set_stage && out.stage == 0x18);
    /* ... or the lobby's (the host resolved Random) ... */
    in.hidden = GR_SSS_FROM_LOBBY;
    in.stage_picked = 1;
    in.stage = 0x0C;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_LEAVE && out.set_stage && out.stage == 0x0C);
    /* ... or none yet: the match's block names it */
    in.stage = MP_GROUP_STAGE_RANDOM;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_LEAVE && !out.set_stage);
    in.stage_picked = 0;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_LEAVE && !out.set_stage);
    /* the lobby back on the character select (the picker's Back, or anyone's L+R+Start) */
    in = idle();
    in.phase = 0;
    CHECK(gr_sss_frame(&in, &out) == GR_SSS_BACK && out.remote);

    /* finding a picked stage on this console's stage select */
    for (i = 0; i < GR_SSS_ICONS; i++) {
        stkind[i] = 0x100 + i;
        unlocked[i] = 2;
    }
    stkind[7] = 0x10;
    stkind[12] = 0x18;
    unlocked[12] = 0;                                 /* not unlocked in this save: a locked pick */
    CHECK(gr_sss_find(stkind, unlocked, 0x10) == 7);
    CHECK(gr_sss_find(stkind, unlocked, 0x18) == GR_SSS_LOCKED);
    CHECK(gr_sss_find(stkind, unlocked, 0x05) == GR_SSS_UNKNOWN);
    unlocked[12] = 1;
    CHECK(gr_sss_find(stkind, unlocked, 0x18) == GR_SSS_LOCKED);
    unlocked[12] = 3;
    CHECK(gr_sss_find(stkind, unlocked, 0x18) == 12);

    printf("group_sss: %s\n", failures ? "FAIL" : "ok");
    return failures != 0;
}
