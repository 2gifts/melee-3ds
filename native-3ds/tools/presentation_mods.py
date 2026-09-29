"""3DS presentation edits (applied after engine_overlays, like gameplay_mods).

Stereo depth of dialog text: the notice window (NtMsgWin: the memory card
prompts and other notices) is a model 29 units in front of a camera whose
convergence plane is 64 units away, so it pops out of the screen. Its SIS
text lies on the convergence plane and therefore stays flat, visibly behind
the window it is written on. Text drawn through that camera gets the
window's parallax instead (port/engine/display.c, mp_display_text_plane).

Same edit format as layout_fixes: {'path/suffix.c': [(old, new, count)]}.
"""
FIXES = {
    'sysdolphin/baselib/hsd_3A76.c': [
        ('void HSD_SisLib_803A84BC(HSD_GObj* gobj, int pass)\n{\n',
         'static void mp_sis_render(HSD_GObj* gobj, int pass);\n'
         '/* Text may take the depth of the panel it is drawn on (display.c). */\n'
         'void HSD_SisLib_803A84BC(HSD_GObj* gobj, int pass)\n{\n'
         '    extern void mp_display_text_begin(void);\n'
         '    extern void mp_display_text_end(void);\n'
         '    mp_display_text_begin();\n'
         '    mp_sis_render(gobj, pass);\n'
         '    mp_display_text_end();\n'
         '}\n'
         'static void mp_sis_render(HSD_GObj* gobj, int pass)\n{\n', 1),
    ],
    'melee/gm/gm_1ADD.c': [
        ('        HSD_GObjObject_80390A70(gobj, HSD_GObj_CameraKind, desc);\n',
         '        HSD_GObjObject_80390A70(gobj, HSD_GObj_CameraKind, desc);\n'
         '        {\n'
         '            /* The window models sit at z = 35; its text at z = 0. */\n'
         '            extern void mp_display_text_plane(HSD_CObj*, float);\n'
         '            mp_display_text_plane(desc, 35.0F);\n'
         '        }\n', 1),
    ],
}


def edits_for(source):
    from pathlib import Path
    path = Path(source).as_posix()
    return [edit for key, edits in FIXES.items() if path.endswith('/' + key) for edit in edits]
