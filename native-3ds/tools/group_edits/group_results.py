"""The results screen after a group match (gmresultplayer.c).

Every console runs vanilla's results screen. In a group, the pads it reads come from the group code
(mp_group_results_pads, port/engine/group/group.c): this console's pad on its own port, without the presses that move
the screen on (Start, and any press before the players' panels); those go to the host, and every console replays the
lobby's results state as presses on the ports concerned. So every screen takes the same steps and ends together, once
every Human player is ready. Outside a group the real pads are read, as before.
"""

HEAD = r'''
/* Group play: the pads this screen reads (see tools/group_edits/group_results.py). Inside the macros the names are the
 * real arrays (a macro does not expand within itself). */
#include <group_engine.h>
static HSD_PadStatus mp_res_copy[4], mp_res_master[4];
static int mp_res_on;
#define HSD_PadCopyStatus (mp_res_on ? mp_res_copy : HSD_PadCopyStatus)
#define HSD_PadMasterStatus (mp_res_on ? mp_res_master : HSD_PadMasterStatus)
'''

FRAME = r'''/* Each frame, before the screen reads any pad: in a group, the pads it reads this frame. */
static void mp_results_frame(ResultsData* data, MatchEnd* end)
{
    MpGroupResults r;
    int i;
    mp_res_on = 0;
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
        ('void fn_80179350(HSD_GObj* arg0)\n{\n',
         FRAME + 'void fn_80179350(HSD_GObj* arg0)\n{\n', 1),
        ('    PAD_STACK(8);\n    match_end = fn_80174274();\n    for (i = 0; i < 6; i++) {\n'
         '        if (data->player_data[i].jobjs[12] != NULL) {\n',
         '    PAD_STACK(8);\n    match_end = fn_80174274();\n    mp_results_frame(data, match_end);\n'
         '    for (i = 0; i < 6; i++) {\n        if (data->player_data[i].jobjs[12] != NULL) {\n', 1),
    ],
}
