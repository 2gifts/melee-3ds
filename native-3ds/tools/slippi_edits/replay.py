"""Slippi replay playback hooks (port/engine/slippi/replay.c).

The call sites are the decomp functions Slippi's playback codes patch, as in
Melee Unlocked's melee-native.patch. Inert without sdmc:/3ds/melee/slippi/replay.bin.
"""
DECL = '#include <slippi_engine.h>\n'


def _decl(anchor):
    return (anchor, DECL + anchor, 1)


FIXES = {
    'melee/ft/fighter.c': [
        _decl('#include "fighter.h"\n'),
        # RestoreGameFrame (8006b0dc): recorded inputs replace the pad's.
        ('            Fighter_Spaghetti_8006AD10_Inner1(fp);\n',
         '            mp_slippi_replay_input(fp);\n'
         '            Fighter_Spaghetti_8006AD10_Inner1(fp);\n', 1),
        # SendGamePostFrame's point: after the camera callback.
        ('void Fighter_UnkCallCameraCallback_8006D9EC(Fighter_GObj* gobj)\n'
         '{\n'
         '    Fighter* fp = GET_FIGHTER(gobj);\n'
         '\n'
         '    if (!fp->x221F_b3) {\n'
         '        ftCommon_8008021C(gobj);\n'
         '        if (fp->cam_cb) {\n'
         '            fp->cam_cb(gobj);\n'
         '        }\n'
         '    }\n',
         'void Fighter_UnkCallCameraCallback_8006D9EC(Fighter_GObj* gobj)\n'
         '{\n'
         '    Fighter* fp = GET_FIGHTER(gobj);\n'
         '\n'
         '    if (!fp->x221F_b3) {\n'
         '        ftCommon_8008021C(gobj);\n'
         '        if (fp->cam_cb) {\n'
         '            fp->cam_cb(gobj);\n'
         '        }\n'
         '    }\n'
         '    mp_slippi_replay_post_frame(fp);\n', 1),
    ],
    'melee/gm/gmvs.c': [
        _decl('#include "gmvs.h"\n'),
        # RestoreGameInfo (8016e748): the replay's match data and seed.
        ('    db_Setup();\n    gm_SetDbPauseInputHandlers(gm_AnyControllerPressedStart,\n',
         '    db_Setup();\n    mp_slippi_replay_start_melee(arg0);\n'
         '    gm_SetDbPauseInputHandlers(gm_AnyControllerPressedStart,\n', 1),
        # IncrementFrameIndex / FetchGameFrame (8016d294), before the pause checks.
        ('        tmp->match_result = gm_GetMatchOutcome();\n'
         '        if (tmp->match_result == OUTCOME_NONE) {\n',
         '        tmp->match_result = gm_GetMatchOutcome();\n'
         '        mp_slippi_replay_scene_think(tmp->match_result);\n'
         '        if (tmp->match_result == OUTCOME_NONE) {\n', 1),
        # RestoreLRAStart: the recording ended here.
        ('    } else {\n        fn_8016CD98(tmp);\n    }\n',
         '    } else {\n'
         '        if (mp_slippi_replay_terminated()) {\n'
         '            fn_8016CF4C(-1, OUTCOME_NO_CONTEST);\n'
         '            return;\n'
         '        }\n'
         '        fn_8016CD98(tmp);\n'
         '    }\n', 1),
        ('    struct EndMeleeData* data = user_data;\n'
         '    VsSceneController* tmp = &controller;\n'
         '    int i;\n\n',
         '    struct EndMeleeData* data = user_data;\n'
         '    VsSceneController* tmp = &controller;\n'
         '    int i;\n\n'
         '    mp_slippi_replay_match_exit();\n', 1),
    ],
    'melee/gm/gm_1A3F.c': [
        _decl('#include "gm_1A3F.h"\n'),
        # Boot to Playback Scene.
        ('    state_machine.routing.prev_mode = GM_COUNT;\n',
         '    mp_slippi_replay_boot_mode(&state_machine.routing.curr_mode);\n'
         '    state_machine.routing.prev_mode = GM_COUNT;\n', 1),
        # The waiting scene's preloads, before the match state's own.
        ('    OSReport("Preload scene %u\\n",state->info.scene_kind);\n',
         '    if (mode->kind == GM_DEBUG_VS && state->id == 1) {\n'
         '        mp_slippi_replay_prepare_scene();\n'
         '    }\n'
         '    OSReport("Preload scene %u\\n",state->info.scene_kind);\n', 1),
    ],
}
