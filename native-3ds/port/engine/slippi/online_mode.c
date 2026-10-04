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
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/sislib.h>
#include <slippi_engine.h>
#include <slippi_net_bridge.h>

enum { ST_CSS, ST_SSS, ST_VS, ST_SPLASH };

static CSSData online_css;
static s8 saved_ckind = CKind_Fox;
static s8 saved_color;
static u8 next_state = ST_VS;
volatile int mp_slippi_test_lose;   /* development: count every game as lost */
static HSD_Text* css_text;
static int slpcss_loaded;
static void** slpcss;   /* slpCSS.dat: chat select, chat message, MODE, connect help */

int mp_slippi_sss_alt_mode(void)
{
    return mp_platform_slippi_ui_alt(0);
}

/* Z on the online SSS (FrozenStadiumToggle). */
void mp_slippi_sss_toggle_alt(void)
{
    lbAudioAx_80024030(mp_platform_slippi_ui_alt(1) ? 1 : 0);
}

static void css_enter(GameModeState* state);
static void css_exit(GameModeState* state);
static void sss_enter(GameModeState* state);
static void sss_exit(GameModeState* state);
static void vs_enter(GameModeState* state);
static void vs_exit(GameModeState* state);
static void splash_enter(GameModeState* state);
static void splash_exit(GameModeState* state);
static u8 splash_data[0x20];
static u32 splash_exit_data;

GameModeState mp_slippi_online_states[] = {
    { ST_CSS, lbDvdPreload_3, 0, css_enter, css_exit, { GS_CSS, &online_css, &online_css } },
    { ST_SSS, lbDvdPreload_3, 0, sss_enter, sss_exit, { GS_SSS, &gmVsMelee_SssData, &gmVsMelee_SssData } },
    { ST_VS, lbDvdPreload_3, 0, vs_enter, vs_exit, { GS_VS, &gmVsMelee_StartData, &gmVsMelee_VsExitInfo } },
    { ST_SPLASH, lbDvdPreload_3, 0, splash_enter, splash_exit, { GS_INTRO_EASY, splash_data, &splash_exit_data } },
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
/* Set when the CSS comes back from the stage pick: no announcer. */
static int css_quiet;

int mp_slippi_css_quiet(void)
{
    return mp_slippi_css_online() && css_quiet;
}

static void css_enter(GameModeState* state)
{
    CSSData* css = gm_GetGameModeStateEnterData(state);
    gm_801B06B0(css, 14, saved_ckind, 4, saved_color, 0x78, 0, 0);
    css->vs.start.players[1].slot_type = Gm_PKind_NA;
    lbDvd_SetupVsPreloadCache();
    next_state = ST_VS;
    css_text = NULL;   /* the CSS's SIS texts are freed with each scene */
    slpcss_loaded = 0;   /* and its archives */
    slpcss = NULL;
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
        css_quiet = 0;
        mp_slippi_css_sheik(0);   /* the vanilla CSS keeps Zelda */
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
     * their sound banks), as Slippi's game-setup preload does. Straight to
     * the match: the VS splash (ST_SPLASH) cost the 3DS about two more
     * seconds of loading than the PC, which then waited at its first frame. */
    mp_slippi_prepare_match_files(mp_slippi_online_pending_block());
    gm_SetNextGameModeStateId(ST_VS);
}

/* ---- the online CSS text (LoadCSSText.asm, UserDisplayFunctions.asm) ----
 * One SIS text on the CSS's canvas 0, scaled 0.1 like Slippi's; the lines
 * come from the native session (slippi_ui_text). Positions, sizes and
 * colours are Slippi's. */
enum { TEXT_LINES = 29 };
static const struct {
    float x, y, size;
    int fit;   /* characters that fit at this size; longer lines are narrowed */
} text_layout[TEXT_LINES] = {
    { 70, 23, 0.5f },                                                     /* header */
    { -112, 20, 0.4f }, { -112, 40, 0.5f }, { -112, 65, 0.4f }, { -112, 85, 0.5f }, /* user */
    { 90, 52, 0.4f }, { 70, 52, 0.45f }, { 90, 75, 0.4f }, { 70, 75, 0.45f },
    { 90, 98, 0.4f }, { 70, 98, 0.45f },                                  /* status lines */
    { 70, 132.5f, 0.4f }, { 70, 152.5f, 0.4f },                           /* Z, chat */
    { -130, -246, 0.5f }, { -50, -246, 0.5f },                            /* Playing: name */
    { 90, 52, 0.4f }, { 90, 70, 0.4f }, { 90, 88, 0.4f }, { 90, 106, 0.4f }, /* error */
    /* Chat messages and the open chat page, larger than Slippi's 0.4: on the
     * 3DS's top screen 0.4 is a 7-pixel font. */
    { 70, 180, 0.55f, 22 }, { 70, 205, 0.55f, 22 }, { 70, 230, 0.55f, 22 }, { 70, 255, 0.55f, 22 },
    { 70, 280, 0.55f, 22 },
    { 70, 128, 0.5f, 24 }, { 70, 154, 0.5f, 24 }, { 70, 180, 0.5f, 24 }, { 70, 206, 0.5f, 24 },
    { 70, 232, 0.5f, 24 },
};
static const GXColor text_colors[7] = {
    { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x8E, 0x91, 0x96, 0xFF }, { 0xFF, 0x00, 0x00, 0xFF },
    { 0x33, 0xFF, 0x2F, 0xFF }, { 0x3C, 0xBC, 0xFF, 0xFF },
    { 229, 76, 76, 0xFF }, { 59, 189, 255, 0xFF },   /* chat: P1, P2 (SPT_CHAT_P1/P2) */
};
static int text_ids[TEXT_LINES], text_color_now[TEXT_LINES];
static char text_now[TEXT_LINES][64];

static void css_text_frame(void)
{
    int i;
    char buf[64];
    if (css_text == NULL) {
        css_text = HSD_SisLib_803A6754(0, 0);
        if (css_text == NULL) {
            return;
        }
        css_text->default_kerning = 1;
        css_text->default_alignment = 0;
        css_text->pos_z = 0.0f;
        css_text->font_size.x = 0.1f;
        css_text->font_size.y = 0.1f;
        for (i = 0; i < TEXT_LINES; i++) {
            text_ids[i] = HSD_SisLib_803A6B98(css_text, text_layout[i].x, text_layout[i].y, "");
            HSD_SisLib_803A7548(css_text, text_ids[i], text_layout[i].size, text_layout[i].size);
            text_color_now[i] = -1;
            text_now[i][0] = 0;
        }
    }
    for (i = 0; i < TEXT_LINES; i++) {
        int color = mp_platform_slippi_ui_text(i, buf, sizeof buf);
        buf[sizeof buf - 1] = 0;
        if (strcmp(buf, text_now[i]) != 0) {
            strcpy(text_now[i], buf);
            HSD_SisLib_803A70A0(css_text, text_ids[i], "%s", buf);
            if (text_layout[i].fit > 0) {
                /* Narrow a long line to the panel's width (characters, a
                 * Shift-JIS pair counting once), keeping its height. */
                int chars = 0, k;
                float sx = text_layout[i].size;
                for (k = 0; buf[k] != 0; k++) {
                    u8 c = (u8) buf[k];
                    if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xEF)) && buf[k + 1] != 0) {
                        k++;
                    }
                    chars++;
                }
                if (chars > text_layout[i].fit) {
                    sx = sx * (float) text_layout[i].fit / (float) chars;
                }
                HSD_SisLib_803A7548(css_text, text_ids[i], sx, text_layout[i].size);
            }
        }
        if (color != text_color_now[i] && color >= 0 && color < 7) {
            text_color_now[i] = color;
            HSD_SisLib_803A74F0(css_text, text_ids[i], (GXColor*) &text_colors[color]);
        }
    }
}

/* Called from mnCharSel_Scene_OnFrame (tools/slippi_edits/online_ui.py) while
 * the CSS runs normally. Returns 1 to leave the CSS (match or stage pick). */
int mp_slippi_css_frame(int ready, PlayerInitData* local, PlayerInitData* remote, u32 trigger, u32 held)
{
    int packed = (ready ? 1 : 0) | ((u8) local->ckind << 8) | ((u8) local->color << 16);
    int flags = mp_platform_slippi_ui_css(packed, (int) trigger, (int) held);
    int opponent = mp_platform_slippi_ui_remote();
    play_sfx(flags);
    css_text_frame();
    if (!slpcss_loaded) {
        /* SceneLoadCSS: Slippi's CSS art, when slippi_files.py installed it. */
        slpcss_loaded = 1;
        if (mp_platform_slippi_has_file("slpCSS.dat")) {
            lbArchive_LoadSymbols("slpCSS.dat", &slpcss, "slpCSS", NULL);
        }
    }
    mp_slippi_css_title(slpcss != NULL ? slpcss[2] : NULL);
    mp_slippi_css_sheik((flags & 8) != 0);
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
    css_quiet = 1;
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
    if (mp_slippi_test_lose) {
        won = 0;   /* UI tests: take the loser's path (stage pick) */
    }
    mp_platform_slippi_ui_event(4 /* RESULT */, won);
    css_quiet = 0;   /* after a game the announcer plays, as in VS mode */
    gm_SetNextGameModeStateId(ST_CSS);
}

/* ---- the VS splash before each game (Slippi's minor scene 4) ----
 * Classic mode's intro screen (GS_INTRO_EASY): this player on the left, the
 * opponent on the right, so the announcer calls the opponent's fighter.
 * SplashScenePrep's template and per-side slots; InitVsSplash's names. */

static void splash_enter(GameModeState* state)
{
    static const u8 template_[16] = { 0x01, 0x78, 0x01, 0x01, 0x01, 0xFF, 0x21, 0x21,
                                      0xFF, 0x21, 0x21, 0xEE, 0x00, 0x00, 0xEE, 0x00 };
    const u8* block = mp_slippi_online_pending_block();
    u8* d = splash_data + 8;
    int local = block != NULL ? block[0x138 + 4] & 3 : 0, i, left = 0, right = 0;
    (void) state;
    memset(splash_data, 0, sizeof splash_data);
    memcpy(d, template_, sizeof template_);
    d[2] = 2;
    d[6] = d[7] = d[9] = d[10] = d[12] = d[13] = d[15] = d[16] = 1;
    for (i = 0; i < 4 && block != NULL; i++) {
        const u8* p = block + 0x60 + 0x24 * i;
        if (p[1] >= 3) {
            continue;   /* no player */
        }
        if (i == local) {
            d[5 + left] = p[0];
            d[11 + left] = p[3];
            left++;
        } else {
            d[8 + right] = p[0];
            d[14 + right] = p[3];
            right++;
        }
    }
    d[3] = (u8) left;
    d[4] = (u8) right;
    splash_data[3] = 0;   /* not teams (the announcer would say "Team ...") */
    splash_data[7] = 0;   /* no event-match staging */
}

static void splash_exit(GameModeState* state)
{
    (void) state;
    gm_SetNextGameModeStateId(ST_VS);
}

/* External stage ids, as the splash names them. */
static const char* stage_name(int id)
{
    static const char* const names[33] = {
        NULL, NULL, "Fountain of Dreams", "Pokemon Stadium", "Princess Peach's Castle", "Kongo Jungle",
        "Brinstar", "Corneria", "Yoshi's Story", "Onett", "Mute City", "Rainbow Cruise", "Jungle Japes",
        "Great Bay", "Hyrule Temple", "Brinstar Depths", "Yoshi's Island", "Green Greens", "Fourside",
        "Mushroom Kingdom I", "Mushroom Kingdom II", NULL, "Venom", "Poke Floats", "Big Blue", "Icicle Mountain",
        NULL, "Flat Zone", "Dream Land", "Yoshi's Island N64", "Kongo Jungle N64", "Battlefield",
        "Final Destination",
    };
    return id >= 0 && id < 33 ? names[id] : NULL;
}

/* After the splash scene's set-up (gm_Scene_IntroEasy_OnEnter, 80186ec4). */
void mp_slippi_splash_text(void)
{
    static const GXColor ports[4] = {
        { 0xE5, 0x4C, 0x4C, 0xFF }, { 0x4B, 0x4C, 0xE5, 0xFF }, { 0xFF, 0xCB, 0x00, 0xFF }, { 0x00, 0xB2, 0x00, 0xFF },
    };
    static const GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
    const u8* block = mp_slippi_online_pending_block();
    HSD_Text* t;
    int local, i, idx;
    char name[64];
    const char* stage;
    if (gm_GetCurrentGameMode() != GM_HANYU_CSS || block == NULL) {
        return;
    }
    local = block[0x138 + 4] & 3;
    t = HSD_SisLib_803A6754(0, 0);
    if (t == NULL) {
        return;
    }
    t->default_kerning = 1;
    t->default_alignment = 0;
    t->pos_z = 0.0f;
    t->font_size.x = 1.0f;
    t->font_size.y = 1.0f;
    for (i = 3; i >= 0; i--) {
        const u8* p = block + 0x60 + 0x24 * i;
        float x = i == local ? 60.0f : 420.0f, y = 80.0f;
        if (p[1] >= 3 || i > 1) {
            continue;
        }
        idx = HSD_SisLib_803A6B98(t, x, y, "P%d", i + 1);
        HSD_SisLib_803A7548(t, idx, 0.5f, 0.5f);
        HSD_SisLib_803A74F0(t, idx, (GXColor*) &ports[i]);
        if (mp_platform_slippi_ui_text(30 + i, name, sizeof name) >= 0) {
            name[sizeof name - 1] = 0;
            idx = HSD_SisLib_803A6B98(t, x + 36.0f, y, "%s", name);
            HSD_SisLib_803A7548(t, idx, 0.5f, 0.5f);
            HSD_SisLib_803A74F0(t, idx, (GXColor*) &white);
        }
    }
    stage = stage_name((block[0xE] << 8) | block[0xF]);
    if (stage != NULL) {
        HSD_Text* s = HSD_SisLib_803A6754(0, 0);
        if (s != NULL) {
            s->pos_x = 238.0f;
            s->pos_y = 440.0f;
            s->pos_z = 0.0f;
            s->box_size_x = 160.0f;
            s->box_size_y = 300.0f;
            s->font_size.x = 1.0f;
            s->font_size.y = 1.0f;
            s->default_alignment = 1;
            s->default_kerning = 1;
            mp_platform_slippi_ui_sis(stage, name, sizeof name);
            idx = HSD_SisLib_803A6B98(s, 0.0f, 0.0f, "%s", name);
            HSD_SisLib_803A7548(s, idx, 0.5f, 0.5f);
        }
    }
}
