"""Slippi Direct menu flow (port/engine/slippi/online_mode.c): 1P -> Online
Play, the online mode in GM_HANYU_CSS's place, and the online CSS hooks, at
the sites Slippi's Online/Menus and Online/CSS codes patch."""
DECL = '#include <slippi_engine.h>\n'

MENU_CODE = r'''
/* ---- Slippi's Online submenu (OnMenuPrep.asm, FN_OnlineSubmenuThink) ----
 * Menu kind 8 is unused in vanilla; Slippi's MnMaAll art has the online
 * options there (Ranked, Unranked, Direct, Teams, Party, Log-in, Log-out,
 * Update). This port supports Direct only: the others stay hidden. */
static u16 mp_slippi_online_descriptions[8] = { 0x645, 0x646, 0x647, 0x64B, 0x64C, 0x648, 0x649, 0x64A };
void mp_slippi_online_menu_think(HSD_GObj* gp);

void mp_slippi_menu_prepare(void)
{
    if (!mp_platform_slippi_menu_files()) {
        return;
    }
    mn_803EB66C[2] = 0x644; /* 1P: "Online Play" description */
    mn_803EB6B0[8].anim_loop = mn_803EB57C;
    mn_803EB6B0[8].start_frame = 140;
    mn_803EB6B0[8].description_indices = mp_slippi_online_descriptions;
    mn_803EB6B0[8].selection_count = 8;
    mn_803EB6B0[8].think = mp_slippi_online_menu_think;
}

void mp_slippi_online_menu_think(HSD_GObj* gp)
{
    u32 buttons;
    MenuExitData* data;
    void (*think)(HSD_GObj*);
    HSD_GObjProc* proc;

    buttons = mn_80229624(4);
    mn_804A04F0.buttons = buttons;
    if (buttons & MenuInput_Confirm) {
        if (mn_804A04F0.hovered_selection == 2) {
            mn_804A04F0.entering_menu = 1;
            sfxForward();
            data = gm_GetCurrentSceneExitData();
            data->pending_mode = GM_HANYU_CSS;
            gm_801A4B60();
        } else {
            lbAudioAx_80024030(3);
        }
    } else if (buttons & MenuInput_Back) {
        sfxBack();
        mn_804A04F0.entering_menu = 0;
        mn_804D6BC8.cooldown = 5;
        mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;
        mn_804A04F0.cur_menu = MENU_KIND_1P;
        mn_804A04F0.hovered_selection = SEL_1P_2;
        HSD_GObj_80390CD4(mn_8022B3A0(3));
        HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);
        if ((think = mn_803EB6B0[MENU_KIND_1P].think)) {
            proc = HSD_GObj_SetupProc(GObj_Create(0, 1, 0x80), think, 0);
            proc->flags_3 = HSD_GObj_804D783C;
        }
    }
    (void) gp;
}
'''

FIXES = {
    # "Slippi Online Scene": the unused major scene 8 becomes the online mode.
    'melee/gm/gmscdata.c': [
        ('        gm_Mode_HanyuCss_States,\n',
         '        mp_slippi_online_states,\n', 1),
        ('static GameMode modes[] = {\n',
         'extern GameModeState mp_slippi_online_states[];\n\n'
         'static GameMode modes[] = {\n', 1),
    ],
    'melee/mn/mnmain.c': [
        # ShowHidden1pOption (802299c4): the hidden 1P entry is Online Play.
        ('    if (menu_kind == MENU_KIND_1P && selection == SEL_1P_2) {\n        return false;\n',
         '    if (menu_kind == MENU_KIND_1P && selection == SEL_1P_2) {\n        return true;\n', 1),
        # OnlineModeOptionSelected (8022d88c). Slippi opens its Online submenu
        # first; Direct is the only mode this port supports.
        ('        case SEL_1P_TRAINING:\n',
         '        case SEL_1P_2:\n'
         '            sfxForward();\n'
         '            if (mp_platform_slippi_menu_files()) {\n'
         '                mp_slippi_menu_prepare();\n'
         '                mn_804D6BC8.cooldown = 5;\n'
         '                mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;\n'
         '                mn_804A04F0.cur_menu = 8;\n'
         '                mn_804A04F0.hovered_selection = 2;\n'
         '                HSD_GObj_80390CD4(mn_8022B3A0(1));\n'
         '                HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);\n'
         '                temp_r3_2 = HSD_GObj_SetupProc(GObj_Create(0, 1, 0x80), mp_slippi_online_menu_think, 0);\n'
         '                temp_r3_2->flags_3 = HSD_GObj_804D783C;\n'
         '            } else {\n'
         '                data = gm_GetCurrentSceneExitData();\n'
         '                data->pending_mode = GM_HANYU_CSS;\n'
         '                gm_801A4B60();\n'
         '            }\n'
         '            break;\n'
         '        case SEL_1P_TRAINING:\n', 1),
        # Online submenu: only Direct (index 2) is available.
        ('    if (menu_kind == MENU_KIND_TOY) {\n',
         '    if (menu_kind == 8) {\n        return selection == 2;\n    }\n'
         '    if (menu_kind == MENU_KIND_TOY) {\n', 1),
        ('/// @brief checks if a menu selection is locked\n', MENU_CODE + '\n/// @brief checks if a menu selection is locked\n', 1),
        ('#include "mnmain.h"\n', DECL + '#include "mnmain.h"\n', 1),
    ],
    # IncreaseTextHeap (801a3f9c): the online CSS's text needs the menus' size.
    'melee/gm/gm_1A3F.c': [
        ('    case GS_CSS:\n        HSD_SisLib_803A6048(0x2400);\n',
         '    case GS_CSS:\n'
         '        HSD_SisLib_803A6048(state_machine.routing.curr_mode == GM_HANYU_CSS ? 0x4800 : 0x2400);\n', 1),
    ],
    'melee/gm/gmmenumode.c': [
        ('#include "gm_1A3F.h"\n', DECL + '#include "gm_1A3F.h"\n', 1),
        ('    previous_mode = gm_GetPreviousGameMode();\n',
         '    previous_mode = gm_GetPreviousGameMode();\n'
         '    mp_slippi_menu_prepare();\n'
         '    if (previous_mode == GM_HANYU_CSS) {\n'
         '        data->menu_kind = mp_platform_slippi_menu_files() ? 8 : MENU_KIND_1P;\n'
         '        data->hovered_selection = mp_platform_slippi_menu_files() ? 2 : SEL_1P_2;\n'
         '        return;\n'
         '    }\n', 1),
    ],
    'melee/mn/mnstagesel.c': [
        ('#include "mnstagesel.h"\n', DECL + '#include "mnstagesel.h"\n', 1),
        ('    if (mnStageSel_804D6CA4 != 0) {\n        mnStageSel_804D6CA4 -= 1;\n        return;\n    }\n',
         '    if (mp_slippi_css_online() && (mnStageSel_804D6CA0 & HSD_PAD_Z) && mnStageSel_804D6CAF == 0) {\n'
         '        mp_slippi_sss_toggle_alt();\n'
         '    }\n'
         '    if (mnStageSel_804D6CA4 != 0) {\n        mnStageSel_804D6CA4 -= 1;\n        return;\n    }\n', 1),
    ],
    'melee/mn/mncharsel.c': [
        ('#include "mncharsel.h"\n', DECL + '#include "mncharsel.h"\n', 1),
        # FetchMatchInfo / HandleInputsOnCSS: the session runs once per frame;
        # it leaves the CSS when both players are locked in (or for the SSS).
        ('    mnCharSel_804D6CEC += 1;\n',
         '    mnCharSel_804D6CEC += 1;\n'
         '    if (mnCharSel_804D6CF6 == 0 && mp_slippi_css_online()) {\n'
         '        u8 port = (u8) mnCharSel_804D6CF0;\n'
         '        if (mp_slippi_css_frame(\n'
         '                mnCharSel_804D6CF7 != 0,\n'
         '                &mnCharSel_804D6CB0->vs.start.players[port],\n'
         '                &mnCharSel_804D6CB0->vs.start.players[(u8) mnCharSel_804D6CF1],\n'
         '                HSD_PadCopyStatus[port].trigger, HSD_PadCopyStatus[port].button))\n'
         '        {\n'
         '            mnCharSel_804D6CF6 = 1;\n'
         '        }\n'
         '    }\n', 1),
        # DisableNametagBox (80261e5c): name entry would rebuild the CSS texts.
        ('                            if ((cursor->x4 != 3 ||\n',
         '                            if (!mp_slippi_css_online() && (cursor->x4 != 3 ||\n', 1),
        # DisableLRSTART (80266bc4).
        ('    if (mn_8022F218() != 0) {\n',
         '    if (!mp_slippi_css_online() && mn_8022F218() != 0) {\n', 1),
        # START belongs to the session (enter code / lock in / pick stage).
        ('        if (mnCharSel_804D6CF2 == 0 && (trigger & HSD_PAD_START)) {\n',
         '        if (!mp_slippi_css_online() && mnCharSel_804D6CF2 == 0 &&\n'
         '            (trigger & HSD_PAD_START))\n'
         '        {\n', 1),
    ],
}
