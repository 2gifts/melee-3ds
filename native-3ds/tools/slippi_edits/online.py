"""Slippi online play hooks (port/engine/slippi/online.c), at the sites
Slippi's Online/Core codes patch. Inert unless config.ini names an opponent."""
DECL = '#include <slippi_engine.h>\n'

FIXES = {
    'sysdolphin/baselib/controller.c': [
        # The raw pads this engine body consumed. UCF reads raw stick history
        # from the queue entry behind qread, but on the 3DS a later pad alarm
        # can already have refilled that slot when the fighters run (the
        # console polls once per frame, before the body). Keep a copy.
        ('    if (p->qcount != 0) {\n        qread = &p->queue->stat[p->qread * 4];\n',
         '    if (p->qcount != 0) {\n        qread = &p->queue->stat[p->qread * 4];\n'
         '        {\n'
         '            extern PADStatus mp_pad_consumed[4];\n'
         '            int k;\n'
         '            for (k = 0; k < 4; k++) {\n'
         '                mp_pad_consumed[k] = qread[k];\n'
         '            }\n'
         '        }\n', 1),
        ('#include "controller.h"\n', DECL + '#include "controller.h"\n', 1),
        # TriggerSendInput (80376A28): send this frame's pad, take the remote
        # one, or drop the sample while waiting for it.
        ('    PADRead(now);\n',
         '    PADRead(now);\n'
         '    if (mp_slippi_online_pad_renew(now)) {\n'
         '        return;\n'
         '    }\n', 1),
    ],
    'melee/gm/gm_1A45.c': [
        ('#include "gm_1A45.h"\n', DECL + '#include "gm_1A45.h"\n', 1),
        ('u32 gm_801A4BB8(void)\n',
         'int mp_slippi_engine_leaving(void)\n{\n    return gm_80479D58.unk_C;\n}\n\n'
         'u32 gm_801A4BB8(void)\n', 1),
        # StartEngineLoop (801A4DE4): checksums, disconnect handling.
        ('            HSD_PerfSetStartTime();\n',
         '            HSD_PerfSetStartTime();\n'
         '            mp_slippi_online_frame_begin();\n'
         '            mp_slippi_replay_body_begin();\n', 1),
        # A tick the render pass will not follow (the 3DS draws less often
        # than it simulates): its gameplay-visible side effects.
        ('            if (temp_r25->unk_C != 0) {\n                break;\n            }\n',
         '            if (temp_r25->unk_C != 0) {\n                break;\n            }\n'
         '            if (i + 1 < pad_queue_count) {\n'
         '                mp_slippi_tick_without_draw();\n'
         '            }\n', 1),
    ],
    'melee/gm/gmvs.c': [
        # InitOnlinePlay (8016E748).
        ('    mp_slippi_replay_start_melee(arg0);\n',
         '    mp_slippi_replay_start_melee(arg0);\n'
         '    mp_slippi_online_start_melee(arg0);\n', 1),
        ('    mp_slippi_replay_match_exit();\n',
         '    mp_slippi_replay_match_exit();\n'
         '    mp_slippi_online_match_exit();\n', 1),
    ],
    'melee/gm/gm_1A3F.c': [
        ('    mp_slippi_replay_boot_mode(&state_machine.routing.curr_mode);\n',
         '    mp_slippi_replay_boot_mode(&state_machine.routing.curr_mode);\n'
         '    mp_slippi_online_boot_mode(&state_machine.routing.curr_mode);\n', 1),
    ],
}

# gmvs.c's controller is file-static: the end-of-match request lives there.
FIXES['melee/gm/gmvs.c'].append((
    'void gm_Scene_Vs_OnExit(void* user_data)\n',
    '/* Slippi online: end the match as a no contest (disconnect). */\n'
    'void mp_slippi_gmvs_end_online(int pauser)\n'
    '{\n'
    '    controller.pauser = (s8) pauser;\n'
    '    controller.match_result = 7;\n'
    '    controller.start.xD = 90;\n'
    '}\n\n'
    'void gm_Scene_Vs_OnExit(void* user_data)\n', 1))
