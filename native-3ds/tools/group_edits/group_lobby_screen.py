"""The "Choose a game." screen: Group on the Solo / Group submenu opens the tournament setup scene (VS -> Tournament
Melee's first screen) as a backdrop while the bottom screen lists the games nearby. Out of the lobby (Back, or the
session ended: port/engine/group/group.c mp_group_css_exit) the console comes back here with the Group list open again;
when the session ended under it, a dialog over the screen says why (port/engine/group/group_dialog.c): OK closes it, and
a game picked on the bottom screen meanwhile goes straight in.

The scene builds as vanilla's (its frame, the header box, the turning bracket behind them) without the rule rows
(Match Type and the rest), and its header reads "Choose a game." in place of "Choose a match style.". Its per-frame
step is ours: the Group list (port/3ds/group/group_menu.c) is polled, Host or Join goes to the lobby (GM_HANYU_CSS), and
B or the list closing (its Back, a network failure) goes back to the submenu with Group hovered (group_menu.py). The
vanilla tournament is unchanged: everything here waits on mp_group_lobby_screen, set only by the submenu.
"""


def sis_title(text):
    """A header as SdTou's "Choose a match style.": scale 0xC7 / 0xCA, centred, kerning; letters, spaces, a full stop."""
    out = [0x0E, 0x00, 0xC7, 0x00, 0xCA, 0x10, 0x16, 0x18]
    for c in text:
        if c == ' ':
            out.append(0x1A)
        elif c == '.':
            out += [0x20, 0xE7]
        elif 'A' <= c <= 'Z':
            out += [0x20, 0x0A + ord(c) - 65]
        elif 'a' <= c <= 'z':
            out += [0x20, 0x24 + ord(c) - 97]
        else:
            raise ValueError(f'no SIS code for {c!r}')
    return out + [0x19, 0x0F, 0x00]


TITLE = 'Choose a game.'

DECLS = r'''
/* ---- "Choose a game." (tools/group_edits/group_lobby_screen.py) ---- */
int mp_group_lobby_screen;   /* set by the Solo / Group submenu: this scene is the Group list's backdrop */
int mp_group_lobby_back;     /* left by B: the menu opens on the submenu, Group hovered */
static int mp_group_lobby_leaving;
static int mp_group_lobby_started;
static char mp_group_lobby_note[96];   /* why the session ended, for its notice */
static const u8 mp_group_lobby_title[] = { /* "''' + TITLE + r'''" */
    ''' + ', '.join(f'0x{b:02X}' for b in sis_title(TITLE)) + r''' };
'''

CODE = r'''

static void mp_group_lobby_leave(GameModeKind mode)
{
    mp_group_lobby_leaving = 1;
    gm_801A4B60();
    gm_ChangeGameModeAfterCurrentScene(mode);
}

/* The soft reset (L+R+Start) leaves through vanilla's handler, which knows nothing of the Group list: close the list
 * and the session first (Back from the list closes the session; Back again closes the page), and any notice. */
static void mp_group_lobby_close(void)
{
    mp_group_dialog_close();
    if (mp_platform_group_waiting()) {
        (void) mp_platform_group_poll(1);
    }
    if (mp_platform_group_waiting()) {
        (void) mp_platform_group_poll(1);
    }
}

static void mp_group_lobby_frame(void)
{
    int r, notice;
    if (mp_group_lobby_leaving) {
        return;
    }
    if (!mp_group_lobby_started) {
        /* Back from the lobby: why the session ended (if it did), and the Group list again at once, under the notice
         * (from the submenu the list is already up) */
        mp_group_lobby_started = 1;
        if (mp_platform_group_lost_note(mp_group_lobby_note, sizeof mp_group_lobby_note)) {
            (void) mp_group_dialog_open(mp_group_lobby_note, "OK", NULL, 0);
        }
        if (gr_choose_opens_list(mp_platform_group_listing(), mp_platform_group_listing() || mp_platform_group_reopen()) &&
            mp_platform_group_open())
        {
            mp_platform_group_pick(1);
        }
    }
    mp_group_dialog_test_frame();   /* tests only (mp_test_dialog) */
    notice = mp_group_dialog_up();
    if (notice) {
        (void) mp_group_dialog_poll();   /* OK (or B) only closes it */
    }
    r = gr_choose_step(mp_platform_group_poll(!notice && (fn_8018F674(4) & PAD_BUTTON_B) != 0),
                       mp_platform_group_listing());
    if (r == GR_CHOOSE_LOBBY) {
        mp_group_dialog_close();             /* a game picked under the notice: straight in */
        mp_group_lobby_leave(GM_HANYU_CSS);  /* hosting, or joined: the lobby */
    } else if (r == GR_CHOOSE_BACK) {
        mp_group_dialog_close();
        sfxBack();                           /* B, the list's Back, or the network failed */
        mp_group_lobby_back = 1;
        mp_group_lobby_leave(GM_MENU);
    }
}

'''

FIXES = {
    'melee/gm/gmtou_0.c': [
        ('#include <sysdolphin/baselib/sislib.h>\n',
         '#include <sysdolphin/baselib/sislib.h>\n#include <group_bridge.h>\n#include <group_engine.h>\n#include <group_rules.h>\n' + DECLS, 1),
        ('/// Tournament Mode per-frame update handler.\n', CODE + '/// Tournament Mode per-frame update handler.\n', 1),
        # No rule rows (Match Type ...): only the frame, the header box and the background.
        ('                (void (*)(HSD_GObj*)) fn_8019175C, 0.0f);\n\n    for (i = 0; i < 6; i++) {\n',
         '                (void (*)(HSD_GObj*)) fn_8019175C, 0.0f);\n\n'
         '    if (mp_group_lobby_screen) {\n        return;\n    }\n    for (i = 0; i < 6; i++) {\n', 1),
        # The header: "Choose a game." (the text's state is reset by 803A6368; its words are ours).
        ('    HSD_SisLib_803A6368(tm->x4E0, 0x4A);\n',
         '    HSD_SisLib_803A6368(tm->x4E0, 0x4A);\n'
         '    if (mp_group_lobby_screen) {\n'
         '        tm->x4E0->sis_buffer = (SIS*) mp_group_lobby_title;\n'
         '    }\n', 1),
        # Our step instead of the rules'. The soft reset still goes through vanilla's handler, the Group list closed first.
        ('    tm = gm_GetTournamentData();\n    r30 = fn_8018F674(4);\n    r29 = fn_8018F640(4);\n',
         '    if (mp_group_lobby_screen) {\n'
         '        if (mn_8022F218() == 0) {\n'
         '            mp_group_lobby_frame();\n'
         '            return;\n'
         '        }\n'
         '        mp_group_lobby_close();   /* the soft reset: vanilla\'s handler below takes the game to the main menu */\n'
         '    }\n'
         '    tm = gm_GetTournamentData();\n    r30 = fn_8018F674(4);\n    r29 = fn_8018F640(4);\n', 1),
        # The first rule row's state, whatever the last tournament left (the header box and background follow it).
        ('    fn_801935B8();\n',
         '    if (mp_group_lobby_screen) {\n'
         '        mp_group_lobby_leaving = 0;\n'
         '        mp_group_lobby_started = 0;\n'
         '        mp_group_dialog_scene();   /* the session-lost notice is shown here */\n'
         '        gm_GetTournamentData()->cur_option = 0;\n'
         '    }\n'
         '    fn_801935B8();\n', 1),
        ('    gm_GetTournamentData()->cur_option = 0x13;\n',
         '    mp_group_lobby_screen = 0;\n    gm_GetTournamentData()->cur_option = 0x13;\n', 1),
    ],
}
