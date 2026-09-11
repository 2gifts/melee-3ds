#include "../3ds/bottom_state.h"
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/mn/types.h>
#include <melee/mn/mnmain.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/sislib_font.h>
#include <string.h>

unsigned mp_bottom_scene = 0xff;
static MPBottomState completed;
static unsigned completed_valid;
void mp_bottom_scene_begin(unsigned scene)
{
    mp_bottom_scene = scene;
    completed_valid = 0;
}
const void* mp_bottom_font(void) { return HSD_SisLib_FontAtlas; }
extern unsigned mp_bottom_css(unsigned);

void mp_bottom_snapshot(MPBottomState* s)
{
    unsigned i;
    memset(s, 0, sizeof(*s));
    s->scene = mp_bottom_scene;
    s->mode = gm_GetCurrentGameMode();
    for (i = 0; i < 4; ++i) s->players[i].kind = 3;
    if (s->scene == 0xff) return;
    if (s->scene == 1) s->menu = mn_804A04F0.cur_menu;
    {
        struct GameRules* rules = gmMainLib_GetGameRules();
        s->rule_stocks = rules->stock_count;
        s->stock_mode = rules->mode == 1;
        s->rule_minutes = s->stock_mode ? rules->stock_time_limit : rules->time_limit;
        s->items = gmMainLib_GetGamePrefs()->item_freq;
    }
    if (s->scene == 8) {
        for (i = 0; i < 4; ++i) {
            unsigned p = mp_bottom_css(i);
            s->players[i].kind = p & 255;
            s->players[i].character = (p >> 8) & 255;
            s->players[i].costume = (p >> 16) & 255;
            s->players[i].color = p >> 24;
        }
    } else if (s->scene == 2 || s->scene == 3 || s->scene == 4 || s->scene == 44) {
        /* GAME/TIME freezes the final frame before asynchronous teardown.
         * File-service waits can still present frames while slots/rules are
         * cleared. Preserve the first completed frame, including its last KO. */
        if (completed_valid) { *s = completed; return; }
        StartMeleeRules* rules = gm_GetStartMeleeRules();
        int seconds = 0;
        s->stock_mode = rules->is_stock && s->scene != 4;
        s->teams = rules->is_teams && s->scene != 4;
        s->stamina = s->mode == 31;
        s->timer = GetMatchTimer(&seconds);
        s->seconds = seconds > 0 ? seconds : 0;
        s->stage = gm_GetStKind();
        for (i = 0; i < 4; ++i) {
            MPBottomPlayer* p = &s->players[i];
            int damage, stocks;
            p->kind = Player_GetPlayerSlotType(i);
            if (p->kind == 3) continue;
            p->character = Player_GetPlayerCharacter(i);
            if ((p->character == 18 || p->character == 19) && Player_80036394(i) == 7)
                p->character = 19;
            p->costume = Player_GetCostumeId(i);
            p->color = i;
            if (s->teams) {
                unsigned team = Player_GetTeam(i);
                if (team < 3) p->color = team == 2 ? 3 : team;
            }
            stocks = Player_GetStocks(i);
            damage = s->stamina ? Player_GetRemainingHP(i) : Player_GetDamage(i);
            p->stocks = stocks > 0 ? stocks : 0;
            p->damage = damage > 0 ? damage : 0;
        }
        if (gmVs_GetController_0()->match_result) {
            completed = *s;
            completed_valid = 1;
        }
    }
}
