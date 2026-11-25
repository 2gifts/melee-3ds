"""Melee -> Solo / Group on the VS menu (port/3ds/group/group_menu.c does the pages and the session).

With group.ini menu=1, choosing Melee asks Solo or Group: Solo is the vanilla flow, Group opens the Host / Join page
and then the lobby (GM_HANYU_CSS, whose scene table runs the vanilla four-door CSS in a group; see group_css.py). The
choice is a bottom-screen touch page; with top_menu=1 (and Slippi's menu files present) it is a top-screen submenu,
menu kind 11 (unused in vanilla), which still shows placeholder art (Slippi's Ranked / Unranked labels) until real
artwork exists. Back from the lobby lands on the VS menu with Melee hovered. Without menu=1 nothing changes.
"""

DECL = '#include <group_bridge.h>\n'

MENU_CODE = r'''
/* ---- Melee -> Solo / Group (tools/group_edits/group_menu.py) ---- */
void mp_group_menu_think(HSD_GObj* gp);

/* Menu kind 11 is unused in vanilla (its header art is VS. Mode with a Tournament Melee tab, its colour the VS menu's).
 * It shows the first two art slots of kind 8, Slippi's Ranked and Unranked labels: placeholder art. */
void mp_group_menu_prepare(void)
{
    mn_803EB6B0[11].anim_loop = mn_803EB57C;
    mn_803EB6B0[11].start_frame = 140;
    mn_803EB6B0[11].description_indices = NULL;
    mn_803EB6B0[11].selection_count = 2;
    mn_803EB6B0[11].think = mp_group_menu_think;
}

/* A / touch picks (group_menu.c does the rest: Solo is vanilla VS, Group opens the lobby), B goes back one level
 * (the Group list's Back first), and the bottom screen shows the Group list once Group is chosen. */
void mp_group_menu_think(HSD_GObj* gp)
{
    u32 buttons;
    MenuExitData* data;
    void (*think)(HSD_GObj*);
    HSD_GObjProc* proc;

    buttons = mn_80229624(4);
    mn_804A04F0.buttons = buttons;
    if (buttons & MenuInput_Confirm) {
        sfxForward();
        mp_platform_group_pick(mn_804A04F0.hovered_selection);
    } else if (buttons & (MenuInput_Up | MenuInput_Down)) {
        sfxMove();
        mn_804A04F0.hovered_selection = mn_804A04F0.hovered_selection == 0 ? 1 : 0;
    }
    switch (mp_platform_group_poll((buttons & MenuInput_Back) != 0)) {
    case 1:
        data = gm_GetCurrentSceneExitData();
        data->pending_mode = GM_VS;
        gm_801A4B60();
        break;
    case 2:
        data = gm_GetCurrentSceneExitData();
        data->pending_mode = GM_HANYU_CSS;
        gm_801A4B60();
        break;
    case -1:
        sfxBack();
        mn_804A04F0.entering_menu = 0;
        mn_804D6BC8.cooldown = 5;
        mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;
        mn_804A04F0.cur_menu = MENU_KIND_VS;
        mn_804A04F0.hovered_selection = SEL_VS_MELEE;
        HSD_GObj_80390CD4(mn_8022B3A0(3));
        HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);
        if ((think = mn_803EB6B0[MENU_KIND_VS].think)) {
            proc = HSD_GObj_SetupProc(GObj_Create(0, 1, 0x80), think, 0);
            proc->flags_3 = HSD_GObj_804D783C;
        }
        break;
    }
    (void) gp;
}
'''

VS_THINK_OLD = ('    buttons = mn_80229624(4);\n    mn_804A04F0.buttons = buttons;\n    if (buttons & MenuInput_Confirm) {\n'
                '        mn_804D6BC8.cooldown = 5;\n        mn_804A04F0.entering_menu = 1;\n'
                '        switch (mn_804A04F0.hovered_selection) {\n        case SEL_VS_MELEE:\n            sfxForward();\n'
                '            data = gm_GetCurrentSceneExitData();\n            data->pending_mode = GM_VS;\n'
                '            gm_801A4B60();\n            break;\n')

VS_THINK_NEW = (
    '    buttons = mn_80229624(4);\n    mn_804A04F0.buttons = buttons;\n'
    '    if (mp_platform_group_fresh()) {\n'
    '        mn_804A04F0.entering_menu = 0;   /* back from a lobby: the first A must work again */\n'
    '        mn_804D6BC8.cooldown = 5;\n'
    '    }\n'
    '    if (!mp_platform_group_waiting() && mp_platform_group_reopen()) {\n'
    '        mn_804A04F0.entering_menu = 1;   /* the session ended: back on the Host / Join page */\n'
    '    }\n'
    '    if (mp_platform_group_waiting()) {\n'
    '        switch (mp_platform_group_poll((buttons & MenuInput_Back) != 0)) {\n'
    '        case 1:\n'
    '            mn_804A04F0.entering_menu = 1;\n'
    '            data = gm_GetCurrentSceneExitData();\n'
    '            data->pending_mode = GM_VS;\n'
    '            gm_801A4B60();\n'
    '            break;\n'
    '        case 2:\n'
    '            mn_804A04F0.entering_menu = 1;\n'
    '            sfxForward();\n'
    '            data = gm_GetCurrentSceneExitData();\n'
    '            data->pending_mode = GM_HANYU_CSS;\n'
    '            gm_801A4B60();\n'
    '            break;\n'
    '        case -1:\n'
    '            sfxBack();\n'
    '            mn_804A04F0.entering_menu = 0;\n'
    '            mn_804D6BC8.cooldown = 5;\n'
    '            break;\n'
    '        }\n'
    '        return;\n'
    '    }\n'
    '    if (buttons & MenuInput_Confirm) {\n        mn_804D6BC8.cooldown = 5;\n        mn_804A04F0.entering_menu = 1;\n'
    '        switch (mn_804A04F0.hovered_selection) {\n        case SEL_VS_MELEE:\n            sfxForward();\n'
    '            if (mp_platform_group_open()) {\n'
    '                if (mp_platform_group_menu() && mp_platform_slippi_menu_files()) {\n'
    '                    mp_group_menu_prepare();\n'
    '                    mn_804D6BC8.cooldown = 5;\n'
    '                    mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;\n'
    '                    mn_804A04F0.cur_menu = 11;\n'
    '                    mn_804A04F0.hovered_selection = 0;\n'
    '                    HSD_GObj_80390CD4(mn_8022B3A0(1));\n'
    '                    HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);\n'
    '                    think = HSD_GObj_SetupProc(GObj_Create(0, 1, 0x80), mp_group_menu_think, 0);\n'
    '                    think->flags_3 = HSD_GObj_804D783C;\n'
    '                } else {\n'
    '                    mn_804A04F0.entering_menu = 0;   /* the bottom-screen page (touch) */\n'
    '                }\n'
    '                break;\n'
    '            }\n'
    '            data = gm_GetCurrentSceneExitData();\n            data->pending_mode = GM_VS;\n'
    '            gm_801A4B60();\n            break;\n')

FIXES = {
    'melee/mn/mnmain.c': [
        ('#include "mnmain.h"\n', DECL + '#include "mnmain.h"\n', 1),
        ('/// @brief checks if a menu selection is locked\n', MENU_CODE + '\n/// @brief checks if a menu selection is locked\n', 1),
        # The Solo / Group submenu's two rows are available.
        ('    if (menu_kind == MENU_KIND_TOY) {\n',
         '    if (menu_kind == 11) {\n        return selection < 2;   /* Solo, Group */\n    }\n'
         '    if (menu_kind == MENU_KIND_TOY) {\n', 1),
        # The VS menu's Melee row asks first; the Solo / Group page polls from this think function.
        (VS_THINK_OLD, VS_THINK_NEW, 1),
    ],
    'melee/gm/gmmenumode.c': [
        ('#include "gm_1A3F.h"\n', DECL + '#include "gm_1A3F.h"\n', 1),
        # Back from the lobby (also the HANYU mode), or from the challenger fight that leaving it may start, lands on the VS
        # menu with Melee hovered, not Slippi's Online menu.
        ('    previous_mode = gm_GetPreviousGameMode();\n',
         '    previous_mode = gm_GetPreviousGameMode();\n'
         '    if ((previous_mode == GM_HANYU_CSS || previous_mode == GM_CHALLENGER_APPROACH) && mp_platform_group_returning()) {\n'
         '        mp_slippi_menu_prepare();\n'
         '        data->menu_kind = MENU_KIND_VS;\n'
         '        data->hovered_selection = SEL_VS_MELEE;\n'
         '        return;\n'
         '    }\n', 1),
    ],
}
