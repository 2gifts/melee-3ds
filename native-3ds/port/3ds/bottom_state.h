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
    /* Save progress, appended so earlier fields keep their offsets:
     * fighters, stages, trophies and events unlocked or cleared, and the
     * build profile (1 fresh save), and which unlockable fighters are still
     * locked (bit i: MP_UNLOCK_ORDER[i] in bottom_draw.c). */
    unsigned fighters, stages, trophies, events, profile, locked;
} MPBottomState;
_Static_assert(sizeof(MPBottomState)==168,"Bottom-screen wire layout");
#endif
