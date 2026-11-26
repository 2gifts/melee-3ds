"""Melee -> Solo / Group on the VS menu (port/3ds/group/group_menu.c does the pages and the session).

With group.ini menu=1, choosing Melee asks Solo or Group: Solo is the vanilla flow, Group opens the Host / Join page
and then the lobby (GM_HANYU_CSS, whose scene table runs the vanilla four-door CSS in a group; see group_css.py). The
choice is a top-screen submenu, menu kind 11 (unused in vanilla: the VS. Mode header with a "Melee" tab, rows "Solo" and
"Group"), whose art is filled into the menu's blank dummy slots each time MnMaAll loads (port/engine/group/menu_art.c).
Without that art (not vanilla's English menu archive) or with group.ini top_menu=0 it is a bottom-screen touch page.
Back from the lobby, or from Solo's character select, lands on the submenu with that row hovered (on the VS menu with
Melee hovered without the submenu); when the session ended under this console, a dialog there says why
(port/engine/group/group_dialog.c) and the Host / Join page opens again after OK. Without menu=1 nothing changes.
"""

DECL = '#include <group_bridge.h>\n#include <group_engine.h>\n#include <group_rules.h>\n'


def sis_text(text):
    """A description line as SdMenu's are written (a sample: SdMenu's "Solo Smash!"): kerning, centred, grey #AAAAAA,
    scale 0xB3, the same codes around it; letters, digits, spaces and full stops only."""
    out = [0x16, 0x10, 0x0C, 0xAA, 0xAA, 0xAA, 0x0E, 0x00, 0xB3, 0x00, 0xB3, 0x06, 0, 0, 0, 0, 0x18]
    for c in text:
        if c == ' ':
            out.append(0x1A)
        elif c == '.':
            out += [0x20, 0xE7]
        elif c.isdigit():
            out += [0x20, ord(c) - 48]
        elif 'A' <= c <= 'Z':
            out += [0x20, 0x0A + ord(c) - 65]
        elif 'a' <= c <= 'z':
            out += [0x20, 0x24 + ord(c) - 97]
        else:
            raise ValueError(f'no SIS code for {c!r}')
    return out + [0x19, 0x0F, 0x0D, 0x00]


def c_bytes(name, text):
    return f'static const u8 {name}[] = {{ /* "{text}" */\n    ' + ', '.join(f'0x{b:02X}' for b in sis_text(text)) + ' };\n'


DESCRIPTIONS = (c_bytes('mp_group_desc_solo', 'Play on this console.') +
                c_bytes('mp_group_desc_group', 'Play with nearby 3DS consoles.'))

MENU_CODE = r'''
/* ---- Melee -> Solo / Group (tools/group_edits/group_menu.py) ---- */
void mp_group_menu_think(HSD_GObj* gp);

/* The submenu's descriptions, written as SdMenu's own (no SdMenu entry is free for them). */
''' + DESCRIPTIONS + r'''
int mp_group_solo_back;   /* Solo was chosen on the submenu: Back from its character select returns there */
extern int mp_group_lobby_screen;   /* group_lobby_screen.py: the "Choose a game." screen */

/* Menu kind 11 is unused in vanilla (its header is VS. Mode, header frames 2400 to 2599, its colour the VS menu's). Its
 * rows are label frames 220 and 222 and its tab frame 2400's: filled with Solo, Group and Melee (menu_art.c). */
void mp_group_menu_prepare(void)
{
    mn_803EB6B0[11].anim_loop = mn_803EB57C;
    mn_803EB6B0[11].start_frame = 220;
    mn_803EB6B0[11].description_indices = NULL;
    mn_803EB6B0[11].selection_count = 2;
    mn_803EB6B0[11].think = mp_group_menu_think;
}

static char mp_group_note[96];   /* why the session ended, for its notice */

int mp_group_top_menu(void)
{
    return mp_platform_group_menu() && mp_group_menu_art_ok();
}

/* Where the menu opens when the scene before it was the lobby (from_lobby) or VS (Solo's character select): the
 * submenu's row to hover, or -1 for vanilla's VS menu. The Solo / Group page is up again (group_menu.c), as when Melee
 * opened it. */
int mp_group_menu_return(int from_lobby)
{
    int row = gr_menu_return_row(from_lobby, mp_group_solo_back);
    mp_group_solo_back = 0;
    if (row < 0 || !mp_group_top_menu() || !mp_platform_group_open()) {
        return -1;
    }
    mp_group_menu_prepare();
    return row;
}

/* From the VS menu's think: open the Solo / Group submenu (kind 11) with `row` hovered. */
void mp_group_menu_enter(int row)
{
    HSD_GObjProc* proc;
    mp_group_menu_prepare();
    mn_804D6BC8.cooldown = 5;
    mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;
    mn_804A04F0.cur_menu = 11;
    mn_804A04F0.hovered_selection = row;
    HSD_GObj_80390CD4(mn_8022B3A0(1));
    HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);
    proc = HSD_GObj_SetupProc(GObj_Create(0, 1, 0x80), mp_group_menu_think, 0);
    proc->flags_3 = HSD_GObj_804D783C;
}

/* A / touch picks (group_menu.c does the rest: Solo is vanilla VS, Group opens the lobby), B goes back one level
 * (the Group list's Back first), and the bottom screen shows the Group list once Group is chosen. */
void mp_group_menu_think(HSD_GObj* gp)
{
    u32 buttons;
    MenuExitData* data;
    void (*think)(HSD_GObj*);
    HSD_GObjProc* proc;

    if (mp_platform_group_fresh()) {
        mn_804A04F0.entering_menu = 0;   /* back from a lobby: the first A must work again */
        mn_804D6BC8.cooldown = 5;
    }
    if (mp_group_dialog_up()) {
        if (mp_group_dialog_poll() != 0) {
            mn_804D6BC8.cooldown = 5;   /* OK: the press that closed it does not pick a row */
        }
        return;
    }
    if (mp_platform_group_lost_note(mp_group_note, sizeof mp_group_note) &&
        mp_group_dialog_open(mp_group_note, "OK", NULL, 0))
    {
        return;   /* why the session ended, over the submenu; OK opens the Host / Join page again */
    }
    mp_platform_group_reopen();
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
        mp_group_solo_back = 1;
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
        return;
    }
    if (mp_platform_group_listing() && !mp_group_lobby_screen) {
        /* The Group list is up (Group, or OK on a lost session's notice): the "Choose a game." screen behind it */
        mp_group_lobby_screen = 1;
        mn_804A04F0.entering_menu = 1;
        data = gm_GetCurrentSceneExitData();
        data->pending_mode = GM_TOURNAMENT;
        gm_801A4B60();
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
    '    if (mp_group_dialog_up()) {\n'
    '        if (mp_group_dialog_poll() == 0) {\n'
    '            return;   /* the notice of why the session ended is up: OK closes it */\n'
    '        }\n'
    '    } else if (!mp_platform_group_waiting() && mp_platform_group_lost_note(mp_group_note, sizeof mp_group_note) &&\n'
    '               mp_group_dialog_open(mp_group_note, "OK", NULL, 0)) {\n'
    '        return;\n'
    '    }\n'
    '    if (!mp_platform_group_waiting() && mp_platform_group_reopen()) {\n'
    '        mn_804A04F0.entering_menu = 1;   /* the session ended: back on the Host / Join page */\n'
    '        if (mp_group_top_menu()) {\n'
    '            mp_group_menu_enter(1);      /* on the Solo / Group submenu, Group hovered */\n'
    '            return;\n'
    '        }\n'
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
    '                if (mp_group_top_menu()) {\n'
    '                    mp_group_menu_enter(0);\n'
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
        # Each time the menu archive loads: the submenu's art, and a new scene for the dialogs.
        ('        HSD_SisLib_803A62A0(3, "SdToy.dat", "SIS_ToyData");\n        gm_801BA8FC();\n',
         '        HSD_SisLib_803A62A0(3, "SdToy.dat", "SIS_ToyData");\n'
         '        mp_group_menu_art(MenMainCursor_Top.matanim_joint, MenMainPanel_Top.matanim_joint);\n'
         '        mp_group_dialog_scene();\n'
         '        gm_801BA8FC();\n', 1),
        ('/// @brief checks if a menu selection is locked\n', MENU_CODE + '\n/// @brief checks if a menu selection is locked\n', 1),
        # The submenu's description line: made as vanilla's (mn_80229A7C), its text written as SdMenu's.
        ('    sis_idx = mn_803EB6B0[menu_kind].description_indices;\n    if (sis_idx != 0) {\n',
         '    sis_idx = mn_803EB6B0[menu_kind].description_indices;\n'
         '    if (sis_idx == 0 && menu_kind == 11) {\n'
         '        text = HSD_SisLib_803A5ACC(0, mn_804D6BB4, -9.5f, 9.1f, 17.0f, 364.68332f, 38.38772f);\n'
         '        data->description = text;\n'
         '        text->font_size.x = 0.0521f;\n'
         '        text->font_size.y = 0.0521f;\n'
         '        HSD_SisLib_803A6368(text, 0);   /* resets the text\'s state; its words are ours: */\n'
         '        text->sis_buffer = (SIS*) (selection == 0 ? mp_group_desc_solo : mp_group_desc_group);\n'
         '    }\n'
         '    if (sis_idx != 0) {\n', 1),
        # The Solo / Group submenu's two rows are available.
        ('    if (menu_kind == MENU_KIND_TOY) {\n',
         '    if (menu_kind == 11) {\n        return selection < 2;   /* Solo, Group */\n    }\n'
         '    if (menu_kind == MENU_KIND_TOY) {\n', 1),
        # The VS menu's Melee row asks first; the Solo / Group page polls from this think function.
        (VS_THINK_OLD, VS_THINK_NEW, 1),
    ],
    'melee/gm/gmmenumode.c': [
        ('#include "gm_1A3F.h"\n', DECL + '#include "gm_1A3F.h"\n\nint mp_group_menu_return(int from_lobby);\nextern int mp_group_lobby_back;\n', 1),
        # Back from the lobby (also the HANYU mode), or from the challenger fight that leaving it may start, lands on the
        # Solo / Group submenu with Group hovered (the VS menu with Melee hovered without it), not Slippi's Online menu.
        # Back from Solo's character select lands on the submenu with Solo hovered.
        ('    previous_mode = gm_GetPreviousGameMode();\n',
         '    previous_mode = gm_GetPreviousGameMode();\n'
         '    if ((previous_mode == GM_HANYU_CSS || previous_mode == GM_CHALLENGER_APPROACH) && mp_platform_group_returning()) {\n'
         '        int row;\n'
         '        mp_slippi_menu_prepare();\n'
         '        row = mp_group_menu_return(1);\n'
         '        data->menu_kind = row >= 0 ? 11 : MENU_KIND_VS;\n'
         '        data->hovered_selection = row >= 0 ? row : SEL_VS_MELEE;\n'
         '        return;\n'
         '    }\n'
         '    if (previous_mode == GM_TOURNAMENT && mp_group_lobby_back) {\n'
         '        int row;\n'
         '        mp_group_lobby_back = 0;\n'
         '        row = mp_group_menu_return(1);   /* B on "Choose a game.": the submenu, Group hovered */\n'
         '        data->menu_kind = row >= 0 ? 11 : MENU_KIND_VS;\n'
         '        data->hovered_selection = row >= 0 ? row : SEL_VS_MELEE;\n'
         '        return;\n'
         '    }\n'
         '    if (previous_mode == GM_VS) {\n'
         '        int row = mp_group_menu_return(0);\n'
         '        if (row >= 0) {\n'
         '            data->menu_kind = 11;\n'
         '            data->hovered_selection = row;\n'
         '            return;\n'
         '        }\n'
         '    }\n', 1),
    ],
}
