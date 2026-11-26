"""3DS presentation edits (applied after engine_overlays, like gameplay_mods).

Stereo depth of dialog text: the notice window (NtMsgWin: the memory card
prompts and other notices) is a model 29 units in front of a camera whose
convergence plane is 64 units away, so it pops out of the screen. Its SIS
text lies on the convergence plane and therefore stays flat, visibly behind
the window it is written on. Text drawn through that camera gets the
window's parallax instead (port/engine/display.c, mp_display_text_plane).

Name entry: a keyboard on the touch screen whenever the game's name entry is
up (port/engine/name_entry.c).

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
    # The bottom-screen keyboard for the name entry (port/engine/name_entry.c):
    # when it opens and closes, its labels, and touches as its buttons.
    'melee/mn/mnnamenew.c': [
        # After the reads, which stay as they are (group play routes their port).
        ('    buttons = (mn_804A04F0.buttons = mn_80229624((u32) mnNameNew_PortInUse));\n',
         '    buttons = (mn_804A04F0.buttons = mn_80229624((u32) mnNameNew_PortInUse));\n'
         '    {\n'
         '        extern u32 mp_name_entry_buttons(u32);\n'
         '        buttons = (mn_804A04F0.buttons = mp_name_entry_buttons(buttons));\n'
         '    }\n', 1),
        ('    buttons = mn_80229624((u32) mnNameNew_PortInUse);\n',
         '    buttons = mn_80229624((u32) mnNameNew_PortInUse);\n'
         '    {\n'
         '        extern u32 mp_name_entry_buttons(u32);\n'
         '        buttons = mp_name_entry_buttons(buttons);\n'
         '    }\n', 1),
        ('    mnNameNew_804D6C08 = gobj;\n',
         '    mnNameNew_804D6C08 = gobj;\n'
         '    {\n'
         '        extern void mp_name_entry_opened(void);\n'
         '        mp_name_entry_opened();\n'
         '    }\n', 1),
        ('    name_index = data->name_index;\n',
         '    name_index = data->name_index;\n'
         '    {\n'
         '        extern void mp_name_entry_closed(void);\n'
         '        mp_name_entry_closed();\n'
         '    }\n', 1),
        ('char* mnNameNew_803EE724[] = { (char*) mnNameNew_804D4F98 };\n',
         'char* mnNameNew_803EE724[] = { (char*) mnNameNew_804D4F98 };\n'
         '\n'
         '/* The keyboard\'s labels: table 0-2 the keys of mode 0-2, 3 and 4 the\n'
         ' * lower and upper variant rows of a key. */\n'
         'const char* mp_name_entry_glyph(int table, int key, int variant)\n'
         '{\n'
         '    MnNameNewDataLayout* layout = mp_name_keyboard_layout();\n'
         '    switch (table) {\n'
         '    case 0:\n'
         '        return layout->x34[key];\n'
         '    case 1:\n'
         '        return layout->xFC[key];\n'
         '    case 2:\n'
         '        return layout->character_bytes[key];\n'
         '    case 3:\n'
         '        return layout->lower_glyphs[key][variant];\n'
         '    default:\n'
         '        return layout->upper_glyphs[key][variant];\n'
         '    }\n'
         '}\n', 1),
    ],
}


def edits_for(source):
    from pathlib import Path
    path = Path(source).as_posix()
    return [edit for key, edits in FIXES.items() if path.endswith('/' + key) for edit in edits]
