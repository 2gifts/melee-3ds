#include <melee/ft/types.h>
#include <melee/pl/player.h>

unsigned mp_platform_tap_jump(void);

/* Tap jump option (bottom screen, CONTROLS): with it off, pushing the stick
 * up no longer jumps for a human-controlled fighter; X/Y still do, and up
 * tilts, smashes and specials are unchanged. CPUs keep vanilla behaviour.
 * Called only after the stick has passed Melee's tap-jump test, so the
 * native bridge is crossed rarely (gameplay_mods.py). */
bool mp_tap_jump_allowed(const Fighter* fp)
{
    return Player_GetPlayerSlotType(fp->player_id) != Gm_PKind_Human || mp_platform_tap_jump();
}
