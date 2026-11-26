/* The few types vanilla's auto handicap rule (fn_80169000, gm_1601.c) reads, for auto_handicap_tests.c: tools/test_group.py
 * compiles the function itself, taken from the decomp's source, against these. */
#ifndef AUTO_HANDICAP_UNIT_H
#define AUTO_HANDICAP_UNIT_H
#include <stdint.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef int32_t s32;
typedef struct {
    u8 pkind;            /* 0 Human, 1 CPU, 3 none */
    u8 is_small_loser;   /* the place: 0 first */
} Standing;
typedef struct {
    Standing player_standings[4];
} MatchEnd;
void fn_80169000(MatchEnd *end, u8 *handicaps);
#endif
