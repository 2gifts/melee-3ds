#include <melee/ft/types.h>
#include <melee/pl/player.h>

unsigned mp_platform_tap_jump(void);

/* Tap jump option (bottom screen, CONTROLS): with it off, pushing the stick
 * up no longer jumps for a human-controlled fighter; X/Y still do, and up
 * tilts, smashes and specials are unchanged. CPUs keep vanilla behaviour.
 * Called only after the stick has passed Melee's tap-jump test, so the
 * native bridge is crossed rarely (gameplay_mods.py). */
int mp_slippi_online_active(void);
int mp_slippi_replay_on(void);

bool mp_tap_jump_allowed(const Fighter* fp)
{
    /* Slippi: the opponent's copy of the game has vanilla tap jump, so an
     * online match (and replay playback) must too, or the games diverge. */
    if (mp_slippi_online_active() || mp_slippi_replay_on()) {
        return true;
    }
    return Player_GetPlayerSlotType(fp->player_id) != Gm_PKind_Human || mp_platform_tap_jump();
}
