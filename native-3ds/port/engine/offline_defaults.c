#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <dolphin/os.h>
#include "native.h"

/* Runs after the save sections are initialized (boot, Erase Data) or read
 * from the memory card; never during play or when revisiting menus.
 * The everything-unlocked build re-applies its unlocks every boot, like
 * Slippi's, but sets its match rules only for a new save so a player's own
 * rule changes persist. The fresh-save build keeps every vanilla default
 * and lock; only UCF (always on) and the single-player C-stick differ. */
void mp_offline_defaults(int new_save){
    if(mp_platform_profile()){
        OSReport("Fresh save profile: vanilla defaults, unlocks earned by play\n");
        return;
    }
    gm_80164F18();gm_8016468C();
    /* Mark unlock notices as already seen as the original full-unlock path
     * does. Otherwise a subsequent menu pass locks More Rules again. */
    gm_8017297C();gm_801741FC();
    gmMainLib_GetSaveData()->x186C=0xff;
    if(!new_save){
        OSReport("Unlocked profile: all characters/stages; saved rules kept; UCF 0.84 enabled\n");
        return;
    }
    GameRules*r=gmMainLib_GetGameRules();
    struct GamePrefs*p=gmMainLib_GetGamePrefs();
    r->mode=1;r->stock_count=4;r->stock_time_limit=8;
    r->handicap=0;r->damage_ratio=10;r->stage_sel=0;
    r->friendly_fire=1;r->pause=1;r->score_display=0;
    p->item_freq=255;p->item_mask=0;
    /* Slippi's SinglesStages selects the six legal stages for Random. */
    p->stage_mask=0xe70000b0u;
    OSReport("Offline defaults: all characters/stages, 4 stocks, 8 minutes, items off, team attack on, pause on; UCF 0.84 enabled\n");
}
