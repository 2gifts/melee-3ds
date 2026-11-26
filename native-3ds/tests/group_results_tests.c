/* Host tests for group play's results screen rules (port/engine/group/results_rules.c): the busy bit, a press kept until
 * the host's results screen, the ready toggles, the lobby's state replayed as presses (both ready ends it, a match with
 * no Human, a cancelled match's wait), the stars, auto handicap (inputs, empty doors, which doors each console's save
 * keeps, cancelled and non-auto matches) and which windows show a console's name. Run by tools/test_group.py. */
#include <group_rules.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("group_results: FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static GrResultsIn screen(int stage, int me)
{
    GrResultsIn in;
    memset(&in, 0, sizeof in);
    in.stage = stage;
    in.panels_live = stage == 3;
    in.humans = 0x3;
    in.me = me;
    in.lob_ok = 1;
    in.lob_busy = 1u << me;
    in.since_end = 1;
    in.phase = 3;
    return in;
}

static GrByte rule_hc[4];
static void fake_rule(void *end, GrByte hc[4])
{
    int i;
    (void) end;
    memcpy(rule_hc, hc, 4);                           /* what vanilla's rule was given */
    for (i = 0; i < 4; i++) {
        hc[i] = (GrByte) (hc[i] + 1);                 /* "the winner up": anything visible */
    }
}

int main(void)
{
    GrResultsIn in;
    GrResultsState st;
    GrResultsOut out;
    int i, sends;

    /* busy: sent while the lobby lacks this console's bit, every 30 frames */
    memset(&st, 0, sizeof st);
    in = screen(1, 1);
    in.lob_busy = 0;
    for (i = sends = 0; i < 61; i++) {
        gr_results_step(&in, &st, &out);
        sends += out.send_busy;
    }
    CHECK(sends == 3);
    in.lob_busy = 0x2;
    gr_results_step(&in, &st, &out);
    CHECK(!out.send_busy);

    /* before the panels nothing this console presses reaches its own screen */
    memset(&st, 0, sizeof st);
    in = screen(2, 1);
    gr_results_step(&in, &st, &out);
    CHECK(out.withhold_mine);
    in = screen(3, 1);
    gr_results_step(&in, &st, &out);
    CHECK(!out.withhold_mine);

    /* a press on step 2 before the host's screen is up (this console's match ended first): kept ... */
    memset(&st, 0, sizeof st);
    in = screen(2, 1);
    in.phase = 2;
    in.trigger = GR_PAD_A;
    gr_results_step(&in, &st, &out);
    CHECK(out.kept && out.presses == 0 && st.early_advance == 2);
    in.trigger = 0;
    gr_results_step(&in, &st, &out);
    CHECK(!out.kept && out.presses == 0);
    /* ... and sent once the host's results screen is up */
    in.phase = 3;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 1 && out.press[0].advance == 2 && out.press[0].ready == 0 && st.early_advance == 0);
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 0);
    /* not sent when the lobby is already that far */
    memset(&st, 0, sizeof st);
    in = screen(2, 1);
    in.lob_advance = 2;
    in.trigger = GR_PAD_A;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 0 && st.early_advance == 0 && !out.kept);
    /* a view from before this console's match ended counts for nothing */
    memset(&st, 0, sizeof st);
    in = screen(2, 1);
    in.since_end = 0;
    in.trigger = GR_PAD_A;
    gr_results_step(&in, &st, &out);
    CHECK(out.kept && out.presses == 0 && out.inject_a == 0);
    /* the cancelled match's wait: Start skips it (advance 1) */
    memset(&st, 0, sizeof st);
    in = screen(0, 0);
    in.canceled = 1;
    in.trigger = GR_PAD_START;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 1 && out.press[0].advance == 1);
    /* ... not after a finished match */
    memset(&st, 0, sizeof st);
    in.canceled = 0;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 0);
    /* a console with no Human in the match presses nothing (a watcher) */
    memset(&st, 0, sizeof st);
    in = screen(2, 2);
    in.trigger = GR_PAD_A;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 0 && st.early_advance == 0);

    /* the panels: Start toggles this console's ready, through the host */
    memset(&st, 0, sizeof st);
    in = screen(3, 1);
    in.trigger = GR_PAD_START;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 1 && out.press[0].ready == 1 && out.press[0].advance == 0);
    in.lob_ready = 0x2;
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 1 && out.press[0].ready == 0);
    in.panels_live = 0;                               /* the panels are still coming in */
    gr_results_step(&in, &st, &out);
    CHECK(out.presses == 0);

    /* the lobby's state as presses: a port whose ready differs gets Start */
    memset(&st, 0, sizeof st);
    in = screen(3, 0);
    in.lob_ready = 0x2;
    in.shown_ready = 0x1;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0x3 && out.plug == 0);
    in.shown_ready = 0x2;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0);
    /* the host's screen ended (lobby back on the character select): everyone ready, the screen plays to its end */
    in.phase = 0;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0x1);                   /* port 0 is not ready yet here */
    /* ... the same with no session */
    in = screen(3, 0);
    in.lob_ok = 0;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0x3);
    in = screen(2, 0);
    in.lost = 1;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_a == 0x1 && out.plug == 0x1);    /* past the intro on the first Human port */
    /* step 2 moved on by the lobby (advance 2) */
    in = screen(2, 1);
    in.humans = 0xC;
    in.lob_advance = 2;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_a == 0x4);
    in.lob_advance = 1;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_a == 0);
    /* the cancelled match's wait, moved on by the lobby */
    in = screen(0, 1);
    in.canceled = 1;
    in.lob_advance = 1;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0x1);
    /* a match with no Human (CPUs only): the first ready port's Start, on a port shown plugged in */
    in = screen(3, 1);
    in.humans = 0;
    in.lob_ready = 0x4;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0x4 && out.plug == 0x4);
    in.lob_ready = 0;
    gr_results_step(&in, &st, &out);
    CHECK(out.inject_start == 0);

    /* the stars: the lobby's plus each Human's KOs, within 0..255; CPUs keep the lobby's */
    {
        GrByte lob[4] = { 3, 250, 7, 0 }, pk[4] = { 0, 0, 1, 3 }, s[4];
        int kos[4] = { 2, 9, 4, 1 };
        gr_results_stars(lob, 1, pk, kos, s);
        CHECK(s[0] == 5 && s[1] == 255 && s[2] == 7 && s[3] == 0);
        gr_results_stars(lob, 0, pk, kos, s);
        CHECK(s[0] == 2 && s[1] == 9 && s[2] == 0);
        kos[0] = -5;                                  /* self-destructs count down, not below 0 */
        gr_results_stars(lob, 1, pk, kos, s);
        CHECK(s[0] == 0);
    }

    /* auto handicap */
    {
        GrByte start[4] = { 0, 3, 9, 12 }, pk[4] = { 0, 1, 0, 3 }, hc[4];
        unsigned write;
        CHECK(gr_results_auto_hc(1, 0, start, pk, 0, 0, fake_rule, NULL, hc, &write));
        CHECK(rule_hc[0] == 5 && rule_hc[1] == 3 && rule_hc[2] == 9 && rule_hc[3] == 5);   /* unknown -> 5 */
        CHECK(hc[0] == 6 && hc[1] == 4 && hc[2] == 10 && hc[3] == 0);                       /* empty door: 0 */
        CHECK(write == 0x1);                          /* a joiner keeps only its own Human door */
        CHECK(gr_results_auto_hc(1, 0, start, pk, 2, 1, fake_rule, NULL, hc, &write) && write == 0x6);   /* host: CPUs */
        /* nothing after a cancelled match or without auto handicap */
        hc[0] = 9;
        CHECK(!gr_results_auto_hc(1, 1, start, pk, 0, 0, fake_rule, NULL, hc, &write) && write == 0 && hc[0] == 0);
        CHECK(!gr_results_auto_hc(0, 0, start, pk, 0, 0, fake_rule, NULL, hc, &write) && write == 0);
    }

    /* the names on the results windows: the ports Human when the match began */
    {
        GrByte slot[4] = { 0, 1, 0, 3 };
        CHECK(gr_results_name_ports(slot) == 0x5);
    }

    printf("group_results: %s\n", failures ? "FAIL" : "ok");
    return failures != 0;
}
