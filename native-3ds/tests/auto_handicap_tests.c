/* Vanilla's auto handicap rule (fn_80169000, compiled from the decomp's gm_1601.c by tools/test_group.py), for the inputs
 * the group's results screen gives it (port/engine/group/group.c, mp_group_results_exit): every door's handicap from
 * the match, 5 where none was known, and the places. The expected values are the rule as vanilla plays it: first place
 * one step down and last place one up, with the special cases at the ends of the 1..9 range; CPUs count as players,
 * empty doors do not. */
#include "auto_handicap_tests.h"
#include <stdio.h>
#include <string.h>

static int fails;

/* kinds: 'H' Human, 'C' CPU, '-' none; place per port; in -> want. */
static void expect(const char *what, const char *kinds, const int place[4], const int in[4], const int want[4])
{
    MatchEnd end;
    u8 hc[4];
    memset(&end, 0, sizeof(end));
    for (int i = 0; i < 4; ++i) {
        end.player_standings[i].pkind = kinds[i] == 'H' ? 0 : kinds[i] == 'C' ? 1 : 3;
        end.player_standings[i].is_small_loser = (u8)place[i];
        hc[i] = (u8)in[i];
    }
    fn_80169000(&end, hc);
    int ok = 1;
    for (int i = 0; i < 4; ++i)
        if (kinds[i] != '-' && hc[i] != want[i]) ok = 0;
    if (!ok) {
        ++fails;
        printf("auto_handicap: FAIL %s: got %d %d %d %d, want %d %d %d %d\n", what, hc[0], hc[1], hc[2], hc[3],
               want[0], want[1], want[2], want[3]);
    }
}

#define A4(a, b, c, d) ((const int[4]){a, b, c, d})

int main(void)
{
    /* two players */
    expect("2 players, both 5", "HH--", A4(0, 1, 0, 0), A4(5, 5, 0, 0), A4(4, 6, 0, 0));
    expect("2 players, port 2 wins", "HH--", A4(1, 0, 0, 0), A4(5, 5, 0, 0), A4(6, 4, 0, 0));
    expect("2 players, a Human and a CPU", "H-C-", A4(1, 0, 0, 0), A4(5, 0, 3, 0), A4(6, 0, 2, 0));
    expect("2 players on ports 2 and 4", "-H-H", A4(0, 0, 0, 1), A4(0, 7, 0, 3), A4(0, 6, 0, 4));
    /* the ends of the range */
    expect("winner at 1: last place two up", "HH--", A4(0, 1, 0, 0), A4(1, 5, 0, 0), A4(1, 7, 0, 0));
    expect("winner at 1, last at 8: no change", "HH--", A4(0, 1, 0, 0), A4(1, 8, 0, 0), A4(1, 8, 0, 0));
    expect("last at 9: winner two down", "HH--", A4(0, 1, 0, 0), A4(5, 9, 0, 0), A4(3, 9, 0, 0));
    expect("winner at 2, last at 9: no change (2 players)", "HH--", A4(0, 1, 0, 0), A4(2, 9, 0, 0), A4(2, 9, 0, 0));
    expect("winner at 9 (from a fixed handicap): down", "HH--", A4(0, 1, 0, 0), A4(9, 5, 0, 0), A4(8, 6, 0, 0));
    /* three and four players: only first and last move, except at the ends */
    expect("3 players, CPU wins", "HHC-", A4(1, 2, 0, 0), A4(5, 5, 5, 0), A4(5, 6, 4, 0));
    expect("3 players with an empty door between", "H-HC", A4(2, 0, 0, 1), A4(5, 0, 5, 5), A4(6, 0, 4, 5));
    expect("3 players, winner 1 and last 8: last up, middle down", "HHH-", A4(0, 1, 2, 0), A4(1, 5, 8, 0), A4(1, 4, 9, 0));
    expect("3 players, winner 2 and last 9: winner and middle down", "HHH-", A4(0, 1, 2, 0), A4(2, 5, 9, 0), A4(1, 4, 9, 0));
    expect("3 players, winner 2 and last 9, middle at 1", "HHH-", A4(0, 1, 2, 0), A4(2, 1, 9, 0), A4(1, 1, 9, 0));
    expect("4 players, all 5", "HCHC", A4(3, 1, 0, 2), A4(5, 5, 5, 5), A4(6, 5, 4, 5));
    expect("4 players, winner 1 and last 8", "HHHH", A4(0, 1, 2, 3), A4(1, 5, 5, 8), A4(1, 4, 5, 9));
    if (fails) return 1;
    printf("auto_handicap: ok\n");
    return 0;
}
