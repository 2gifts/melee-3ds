"""The results screen after a group match (gmresultplayer.c).

Every console runs vanilla's results screen. In a group, the pads it reads come from the group code
(mp_group_results_pads, port/engine/group/group.c): this console's pad on its own port, without the presses that move
the screen on (Start, and any press before the players' panels); those go to the host, and every console replays the
lobby's results state as presses on the ports concerned. So every screen takes the same steps and ends together, once
every Human player is ready. Outside a group the real pads are read, as before.

Names: under each player window vanilla shows the fighter's name as a texture (player_data[n].jobjs[5]).
In a group, a window whose port a console played (mp_group_result_name: its name as the match began, kept if that
player dropped to a CPU) hides that graphic and shows the console's name instead, a SIS text in the game's font that
follows the joint's place each frame. CPU doors keep the fighter's name.
"""

HEAD = r'''
/* Group play: the pads this screen reads (see tools/group_edits/group_results.py). Inside the macros the names are the
 * real arrays (a macro does not expand within itself). */
#include <group_engine.h>
static HSD_PadStatus mp_res_copy[4], mp_res_master[4];
static int mp_res_on;
#define HSD_PadCopyStatus (mp_res_on ? mp_res_copy : HSD_PadCopyStatus)
#define HSD_PadMasterStatus (mp_res_on ? mp_res_master : HSD_PadMasterStatus)
#include <string.h>
#include <sysdolphin/baselib/sislib.h>
/* The names under the windows. Tests read mp_res_label (the game text drawn per window, "" = vanilla's label) and
 * mp_res_named (bit n: window n's fighter-name graphic is hidden). The size and offsets are in the screen's units,
 * from the name joint's place (about 3.5 px per unit up and down), set against vanilla's label on a CPU's window. The
 * width is the text box's (SIS fitting narrows a longer name into it): about 56 px, inside the window as vanilla's
 * longest fighter names are. */
char mp_res_label[4][24];
unsigned char mp_res_named;
HSD_Text* mp_res_text[4];
float mp_res_size = 0.05f, mp_res_width = 10.6f, mp_res_dx = 0.0f, mp_res_dy = -0.8f;
float mp_res_drawn[4];   /* each text's width as the renderer last measured it, after fitting (HSD_Text.current_width) */
'''

NAMES = r'''/* Each window's name text where its name joint is now (the windows slide in). */
static void mp_results_names_place(ResultsData* data)
{
    int i;
    for (i = 0; i < 4; i++) {
        Vec3 pos;
        HSD_Text* t = mp_res_text[i];
        if (t == NULL) {
            continue;
        }
        lb_8000B1CC(data->player_data[i].jobjs[5], NULL, &pos);
        t->pos_x = pos.x + mp_res_dx;
        t->pos_y = -pos.y + mp_res_dy;
        t->pos_z = pos.z;
        mp_res_drawn[i] = t->current_width;
        t->font_size.x = mp_res_size;
        t->font_size.y = mp_res_size;
        t->box_size_x = mp_res_width / mp_res_size;
    }
}

/* Once the windows are set up: in a group, the console's name in place of the fighter's on every window a console
 * played. */
static void mp_results_names(ResultsData* data, MatchEnd* end)
{
    int i;
    mp_res_named = 0;
    for (i = 0; i < 4; i++) {
        HSD_Text* t;
        GXColor col;
        mp_res_text[i] = NULL;
        mp_res_label[i][0] = 0;
        if (!mp_group_mode() || end->player_standings[i].pkind == Gm_PKind_NA ||
            !mp_group_result_name(i, mp_res_label[i], sizeof mp_res_label[i]))
        {
            mp_res_label[i][0] = 0;
            continue;
        }
        t = HSD_SisLib_803A6754(0, 0);
        if (t == NULL) {
            mp_res_label[i][0] = 0;
            continue;
        }
        t->default_alignment = 1;   /* centred */
        t->default_fitting = 1;     /* squeezed into the box when too wide */
        t->default_kerning = 1;
        t->box_size_y = 32.0f;
        col = fn_8017507C(i);       /* white, or the team's colour, as the window's other texts */
        HSD_SisLib_803A74F0(t, HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "%s", mp_res_label[i]), &col);
        mp_res_text[i] = t;
        HSD_JObjSetFlagsAll(data->player_data[i].jobjs[5], JOBJ_HIDDEN);
        mp_res_named |= (unsigned char) (1 << i);
    }
    mp_results_names_place(data);
}

'''

FRAME = r'''/* Each frame, before the screen reads any pad: in a group, the pads it reads this frame. */
static void mp_results_frame(ResultsData* data, MatchEnd* end)
{
    MpGroupResults r;
    int i;
    mp_res_on = 0;
    if (data->x1 < 1) {
        /* this screen's windows are not set up yet: texts from an earlier results screen went with its scene */
        memset(mp_res_text, 0, sizeof mp_res_text);
        mp_res_named = 0;
    } else {
        mp_results_names_place(data);
    }
    if (!mp_group_mode()) {
        return;
    }
    r.stage = data->x1;
    r.panels_live = data->x1 == 3 && data->x0_23 == 2;
    r.ready = 0;
    for (i = 0; i < 4; i++) {
        if (data->player_data[i].x0_0) {
            r.ready |= (unsigned char) (1 << i);
        }
    }
    mp_group_results_pads(&r, end, mp_res_copy, mp_res_master);
    mp_res_on = 1;
}

'''

FIXES = {
    'melee/gm/gmresultplayer.c': [
        ('#include <sysdolphin/baselib/wobj.h>\n',
         '#include <sysdolphin/baselib/wobj.h>\n' + HEAD, 1),
        ('void fn_80178BB4(HSD_GObj* gobj)\n{\n',
         NAMES + 'void fn_80178BB4(HSD_GObj* gobj)\n{\n', 1),
        ('    fn_80178BB4_init_players(data, match_end, &i, &ko_count);\n',
         '    fn_80178BB4_init_players(data, match_end, &i, &ko_count);\n    mp_results_names(data, match_end);\n', 1),
        ('void fn_80179350(HSD_GObj* arg0)\n{\n',
         FRAME + 'void fn_80179350(HSD_GObj* arg0)\n{\n', 1),
        ('    PAD_STACK(8);\n    match_end = fn_80174274();\n    for (i = 0; i < 6; i++) {\n'
         '        if (data->player_data[i].jobjs[12] != NULL) {\n',
         '    PAD_STACK(8);\n    match_end = fn_80174274();\n    mp_results_frame(data, match_end);\n'
         '    for (i = 0; i < 6; i++) {\n        if (data->player_data[i].jobjs[12] != NULL) {\n', 1),
    ],
}
