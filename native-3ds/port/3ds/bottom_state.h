#ifndef MP_BOTTOM_STATE_H
#define MP_BOTTOM_STATE_H
/* Word-only wire format: engine writes BE, native display converts to LE.
 * Never pass engine-owned object pointers across the UI boundary. */
typedef struct {
    unsigned kind, character, costume, color, stocks, damage;
} MPBottomPlayer;
typedef struct {
    unsigned scene, mode, stock_mode, teams, seconds, timer, stage;
    unsigned rule_stocks, rule_minutes, items, stamina, menu;
    MPBottomPlayer players[4];
} MPBottomState;
_Static_assert(sizeof(MPBottomState)==144,"Bottom-screen wire layout");
#endif
