/* Slippi online Direct mode: the menu flow, engine side.
 *
 * Like Slippi's "Slippi Online Scene" (Online/Slippi Online Scene/main.asm),
 * this takes over the unused major scene GM_HANYU_CSS (8):
 *   CSS (the 1P-style CSS with only your door, as Slippi's EVENT_MATCH prep)
 *   -> SSS (only the loser of the last game picks a stage)
 *   -> VS (the match; the game info block negotiated with the opponent)
 *   -> back to the CSS, still connected (Slippi has no results screen here).
 * The session logic (code entry, search, lock-in, chat) is native, in
 * port/3ds/slippi/slippi_ui.c. Each CSS frame this hands it the local choice
 * and the port's buttons and gets back what to do.
 * Entered from 1P -> Online Play (tools/slippi_edits/online_ui.py).
 */
#include <string.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>

enum { ST_CSS, ST_SSS, ST_VS };

static CSSData online_css;
static s8 saved_ckind = CKind_Fox;
static s8 saved_color;
static u8 next_state = ST_VS;
static int sss_alt;   /* frozen Stadium (Z on the SSS) */

int mp_slippi_sss_alt_mode(void)
{
    return sss_alt;
}

void mp_slippi_sss_toggle_alt(void)
{
    sss_alt ^= 1;
}

static void css_enter(GameModeState* state);
static void css_exit(GameModeState* state);
static void sss_enter(GameModeState* state);
static void sss_exit(GameModeState* state);
static void vs_enter(GameModeState* state);
static void vs_exit(GameModeState* state);

GameModeState mp_slippi_online_states[] = {
    { ST_CSS, lbDvdPreload_3, 0, css_enter, css_exit, { GS_CSS, &online_css, &online_css } },
    { ST_SSS, lbDvdPreload_3, 0, sss_enter, sss_exit, { GS_SSS, &gmVsMelee_SssData, &gmVsMelee_SssData } },
    { ST_VS, lbDvdPreload_3, 0, vs_enter, vs_exit, { GS_VS, &gmVsMelee_StartData, &gmVsMelee_VsExitInfo } },
    { GM_GAMEMODESTATE_TERMINATE },
};

static void play_sfx(int flags)
{
    if (flags & 0x400) {
        lbAudioAx_80024030(3);
    } else if (flags & 0x100) {
        lbAudioAx_80024030(1);
    } else if (flags & 0x200) {
        lbAudioAx_80024030(0);
    } else if (flags & 0x800) {
        lbAudioAx_80024030(2);
    }
}

/* CSSScenePrep: restore the last fighter, 1P-style CSS (match type 14, the
 * event-match layout Slippi uses), clear the preload cache. */
static void css_enter(GameModeState* state)
{
    CSSData* css = gm_GetGameModeStateEnterData(state);
    gm_801B06B0(css, 14, saved_ckind, 4, saved_color, 0x78, 0, 0);
    css->vs.start.players[1].slot_type = Gm_PKind_NA;
    lbDvd_SetupVsPreloadCache();
    next_state = ST_VS;
    mp_platform_slippi_ui_event(1 /* CSS_ENTER */, 0);
}

static void css_exit(GameModeState* state)
{
    CSSData* css = gm_GetGameModeStateExitData(state);
    s8 ckind;
    u8 color;
    if (css->pending_scene_change == 2) {
        /* B held: back to the menus. The connection closes. */
        mp_platform_slippi_ui_event(2 /* LEAVE */, 0);
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);
        return;
    }
    gm_801B0730(css, &ckind, NULL, &color, NULL, NULL);
    saved_ckind = ckind;
    saved_color = (s8) color;
    if (next_state == ST_SSS) {
        gm_SetNextGameModeStateId(ST_SSS);
        return;
    }
    /* Both locked in: load what the match needs (both fighters, the stage,
     * their sound banks), as Slippi's game-setup preload does. */
    mp_slippi_prepare_match_files(mp_slippi_online_pending_block());
    gm_SetNextGameModeStateId(ST_VS);
}

/* Called from mnCharSel_Scene_OnFrame (tools/slippi_edits/online_ui.py) while
 * the CSS runs normally. Returns 1 to leave the CSS (match or stage pick). */
int mp_slippi_css_frame(int ready, PlayerInitData* local, PlayerInitData* remote, u32 trigger, u32 held)
{
    int packed = (ready ? 1 : 0) | ((u8) local->ckind << 8) | ((u8) local->color << 16);
    int flags = mp_platform_slippi_ui_css(packed, (int) trigger, (int) held);
    int opponent = mp_platform_slippi_ui_remote();
    play_sfx(flags);
    /* The opponent's fighter, once known, preloads while we wait, so the
     * match loads faster; it is never shown. Straight into the preload
     * cache's second slot: marking players[1] present would make the 1-door
     * CSS read a cursor token that this layout never creates. */
    (void) remote;
    {
        struct GameCache* cache = &lbDvd_GetPreloadCacheScene()->game_cache;
        if (opponent >= 0) {
            cache->entries[1].char_id = (s8) (opponent & 0xFF);
            cache->entries[1].color = (u8) (opponent >> 8);
        } else {
            cache->entries[1].char_id = ChKind_None;
        }
    }
    if (flags & 4 /* SSS */) {
        next_state = ST_SSS;
        return 1;
    }
    if ((flags & 2 /* START */) && mp_slippi_online_take_match()) {
        next_state = ST_VS;
        return 1;
    }
    return 0;
}

int mp_slippi_css_online(void)
{
    return gm_GetCurrentGameMode() == GM_HANYU_CSS;
}

static void sss_enter(GameModeState* state)
{
    SSSData* sss = gm_GetGameModeStateEnterData(state);
    sss->vs = online_css.vs;
    sss->force_stage_id = -1;
    sss->no_lras = 1;
    sss->start_game = 0;
}

static void sss_exit(GameModeState* state)
{
    SSSData* sss = gm_GetGameModeStateExitData(state);
    if (sss->start_game) {
        int stage = sss->vs.start.rules.stkind;
        mp_platform_slippi_ui_event(3 /* STAGE */, stage | (mp_slippi_sss_alt_mode() << 16));
    } else {
        mp_platform_slippi_ui_event(3 /* STAGE */, -1);
    }
    gm_SetNextGameModeStateId(ST_CSS);
}

static void vs_enter(GameModeState* state)
{
    (void) state;
    gm_LoadAnnouncer();
    mp_platform_slippi_ui_event(5 /* MATCH */, 0);
}

/* VSSceneDecide + CheckIfWonLastGame: back to the CSS; who picks next. */
static void vs_exit(GameModeState* state)
{
    MatchExitInfo* exit = gm_GetGameModeStateExitData(state);
    MatchEnd* end = &exit->match_end;
    int local = mp_slippi_online_local_index();
    int won = 0;
    if (end->outcome == 7) {
        /* L+R+A+Start: whoever quit picks the next stage. */
        won = ((u8*) end)[0] != local;
    } else if (end->outcome != 0) {
        won = end->player_standings[local].is_big_loser == 0;
    }
    mp_platform_slippi_ui_event(4 /* RESULT */, won);
    gm_SetNextGameModeStateId(ST_CSS);
}
