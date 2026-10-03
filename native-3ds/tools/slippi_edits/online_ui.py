"""Slippi Direct menu flow (port/engine/slippi/online_mode.c): 1P -> Online
Play, the online mode in GM_HANYU_CSS's place, and the online CSS hooks, at
the sites Slippi's Online/Menus and Online/CSS codes patch."""
DECL = '#include <slippi_engine.h>\n'

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
         '            data = gm_GetCurrentSceneExitData();\n'
         '            data->pending_mode = GM_HANYU_CSS;\n'
         '            gm_801A4B60();\n'
         '            break;\n'
         '        case SEL_1P_TRAINING:\n', 1),
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
