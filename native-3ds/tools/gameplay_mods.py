"""Gameplay mods shipped in both builds (applied after engine_overlays).

C-stick in single-player modes: vanilla Melee ignores a human player's
C-stick in 1P modes (Classic, Adventure, All-Star, Events, Stadium) and uses
C-stick up/down to zoom the camera instead. Like the popular Gecko code,
human players get C-stick smash and aerial attacks there too, and the 1P
camera zoom no longer reads the C-stick. CPU fighters keep their vanilla
1P behaviour, and the pause camera is unchanged.

Tap jump option: the bottom screen's CONTROLS page can turn tap jump off.
Then a human player's stick-up no longer jumps (on the ground, from ledges,
dashes and tumble, or in the air); X/Y still jump. See port/engine/tap_jump.c.

Same edit format as layout_fixes: {'path/suffix.c': [(old, new, count)]}.
"""
TAP_JUMP_DECL = ('#include <melee/ft/types.h>\n',
                 '#include <melee/ft/types.h>\n'
                 'extern bool mp_tap_jump_allowed(const Fighter* fp);\n', 1)
FIXES = {
    'melee/ft/kinds/ftCommon/ftCo_Jump.c': [
        TAP_JUMP_DECL,
        ('    if ((fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold) &&\n'
         '        (fp->x671_timer_lstick_tilt_y < p_ftCommonData->tap_jump_window))\n'
         '    {\n'
         '        return JumpInput_LStick;\n',
         '    if ((fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold) &&\n'
         '        (fp->x671_timer_lstick_tilt_y < p_ftCommonData->tap_jump_window) &&\n'
         '        mp_tap_jump_allowed(fp))\n'
         '    {\n'
         '        return JumpInput_LStick;\n', 1),
        ('         p_ftCommonData->relaxed_tap_jump_threshold) &&\n'
         '        (fp->x671_timer_lstick_tilt_y < p_ftCommonData->tap_jump_window))\n',
         '         p_ftCommonData->relaxed_tap_jump_threshold) &&\n'
         '        (fp->x671_timer_lstick_tilt_y < p_ftCommonData->tap_jump_window) &&\n'
         '        mp_tap_jump_allowed(fp))\n', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_JumpAerial.c': [
        TAP_JUMP_DECL,
        ('        ((fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold &&\n'
         '          fp->x671_timer_lstick_tilt_y < p_ftCommonData->tap_jump_window) ||\n',
         '        ((fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold &&\n'
         '          fp->x671_timer_lstick_tilt_y < p_ftCommonData->tap_jump_window &&\n'
         '          mp_tap_jump_allowed(fp)) ||\n', 1),
    ],
    'melee/ft/fighter.c': [
        ('                if (DbLevel < DbLKind_DebugRom &&\n'
         '                    gm_IsCurrently1PMode_inline() == 0)\n'
         '                {\n'
         '                    SET_STICKS(\n'
         '                        fp->input.cstick[0].x, fp->input.cstick[0].y,\n'
         '                        HSD_PadGameStatus[fp->x618_player_id].nml_subStickX,',
         '                /* Mod: human C-stick also in single-player modes. */\n'
         '                if (DbLevel < DbLKind_DebugRom)\n'
         '                {\n'
         '                    SET_STICKS(\n'
         '                        fp->input.cstick[0].x, fp->input.cstick[0].y,\n'
         '                        HSD_PadGameStatus[fp->x618_player_id].nml_subStickX,', 1),
    ],
    'melee/cm/camera.c': [
        ('            var_f1 = HSD_PadCopyStatus[idx].nml_subStickY;',
         '            /* Mod: the C-stick attacks in 1P modes; no camera zoom. */\n'
         '            (void) idx;\n'
         '            var_f1 = 0.0f;', 1),
    ],
}


def edits_for(source):
    from pathlib import Path
    path = Path(source).as_posix()
    return [edit for key, edits in FIXES.items() if path.endswith('/' + key) for edit in edits]
