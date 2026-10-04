"""Debugging: a CPU command trace for replay runs (replay.c, dump.txt window)."""
DECL = 'struct Fighter;\nvoid mp_slippi_replay_cpu_trace(struct Fighter* fp, int kind, int value);\n'

FIXES = {
    'melee/ft/ftcmdscript.c': [
        ('#include "ftcmdscript.h"\n', DECL + '#include "ftcmdscript.h"\n', 1),
        ('    *data->write_pos = cmd;\n',
         '    mp_slippi_replay_cpu_trace(fp, 0, cmd);\n    *data->write_pos = cmd;\n', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_0A01.c': [
        ('#include "ftCo_0A01.h"\n', DECL + '#include "ftCo_0A01.h"\n', 1),
        ('        ftCo_800ADC28(fp);\n        cmd = data->x18;\n',
         '        ftCo_800ADC28(fp);\n        cmd = data->x18;\n'
         '        mp_slippi_replay_cpu_trace(fp, 1, cmd);\n', 1),
    ],
}
