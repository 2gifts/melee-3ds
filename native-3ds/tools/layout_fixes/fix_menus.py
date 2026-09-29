"""Menu screens (Rumble, Erase Data, Trophy info, Records counts)."""

FIXES = {
    'melee/mn/mnvibration.c': [
        # cursor_anim (+0x18) is the separate 803EECF8 object.
        ('''    MnVibrationDataLayout* floats =
        (MnVibrationDataLayout*) &mnVibration_803EECE0;
''', '''    /* The matching build placed 803EECF8 right after 803EECE0 (the old
     * cursor_anim at +0x18). ARM does not; name the object. */
''', 1),
        ('floats->cursor_anim.', 'mnVibration_803EECF8.', 3),
        # Assert strings at +0x30/+0x48/+0x58 are their own statics.
        ('''    MnVibrationDataLayout* layout =
        (MnVibrationDataLayout*) &mnVibration_803EECE0;
''', '', 1),
        ('''        OSReport(layout->user_data_error);
        __assert(layout->file_name, 0x3A7, layout->user_data_name);''',
         '''        /* These strings followed 803EECE0 contiguously in the matching
         * build; ARM does not. Use the objects themselves. */
        OSReport(mnVibration_803EED10);
        __assert(mnVibration_803EED28, 0x3A7, mnVibration_803EED38);''', 1),
        # assets[1..3] were the next BSS blocks; the name strings followed
        # 803EECE0 in .data. Unreferenced statics are absent on ARM, and the
        # blocks read only elsewhere fold to NULL.
        ('''    MnVibrationJointAssets* assets;
    MnVibrationDataLayout* strings;

    strings = (MnVibrationDataLayout*) &mnVibration_803EECE0;
    assets = (&mnVibration_804A0868);
''', '', 1),
        ('''    lbArchive_LoadSections(
        archive, &assets[3].joint, strings->convi_top_joint,
        &assets[3].animjoint, strings->convi_top_animjoint, &assets[3].matanim,
        strings->convi_top_matanim_joint, &assets[3].shapeanim,
        strings->convi_top_shapeanim_joint,

        &assets[1].joint, strings->ctlvi_top_joint, &assets[1].animjoint,
        strings->ctlvi_top_animjoint, &assets[1].matanim,
        strings->ctlvi_top_matanim_joint, &assets[1].shapeanim,
        strings->ctlvi_top_shapeanim_joint,

        &assets[2].joint, strings->onoffvi_top_joint, &assets[2].animjoint,
        strings->onoffvi_top_animjoint, &assets[2].matanim,
        strings->onoffvi_top_matanim_joint, &assets[2].shapeanim,
        strings->onoffvi_top_shapeanim_joint,

        &assets[0].joint, strings->cursorvi_top_joint,

        NULL);''', '''    /* The matching build placed the four asset blocks (804A0868..0898)
     * and the symbol names (after 803EECE0) contiguously; ARM does not.
     * Load into and from the named objects the offsets denoted. */
    lbArchive_LoadSections(
        archive, &mnVibration_804A0898.joint, mnVibration_803EED44,
        &mnVibration_804A0898.animjoint, mnVibration_803EED5C,
        &mnVibration_804A0898.matanim, mnVibration_803EED78,
        &mnVibration_804A0898.shapeanim, mnVibration_803EED98,

        &mnVibration_804A0878.joint, mnVibration_803EEDBC,
        &mnVibration_804A0878.animjoint, mnVibration_803EEDD4,
        &mnVibration_804A0878.matanim, mnVibration_803EEDF0,
        &mnVibration_804A0878.shapeanim, mnVibration_803EEE10,

        &mnVibration_804A0888.joint, mnVibration_803EEE34,
        &mnVibration_804A0888.animjoint, mnVibration_803EEE50,
        &mnVibration_804A0888.matanim, mnVibration_803EEE70,
        &mnVibration_804A0888.shapeanim, mnVibration_803EEE94,

        &mnVibration_804A0868.joint, mnVibration_803EEEB8,

        NULL);''', 1),
    ],
    'melee/mn/mndatadel.c': [
        # assets[1], assets[2] were the next BSS blocks (CursorDl, WarCmn).
        ('''        &assets[1].joint, "MenMainCursorDl_Top_joint", &assets[1].animjoint,
        "MenMainCursorDl_Top_animjoint", &assets[1].matanim_joint,
        "MenMainCursorDl_Top_matanim_joint", &assets[1].shapeanim_joint,
        "MenMainCursorDl_Top_shapeanim_joint", &assets[2].joint,
        "MenMainWarCmn_Top_joint", &assets[2].animjoint,
        "MenMainWarCmn_Top_animjoint", &assets[2].matanim_joint,
        "MenMainWarCmn_Top_matanim_joint", &assets[2].shapeanim_joint,
        "MenMainWarCmn_Top_shapeanim_joint", 0);''',
         '''        /* The matching build placed 804A0928 and 804A0938 right after
         * 804A0918 (assets[1], assets[2]); ARM does not. */
        &mnDataDel_804A0928.joint, "MenMainCursorDl_Top_joint",
        &mnDataDel_804A0928.animjoint, "MenMainCursorDl_Top_animjoint",
        &mnDataDel_804A0928.matanim_joint, "MenMainCursorDl_Top_matanim_joint",
        &mnDataDel_804A0928.shapeanim_joint,
        "MenMainCursorDl_Top_shapeanim_joint", &mnDataDel_804A0938.joint,
        "MenMainWarCmn_Top_joint", &mnDataDel_804A0938.animjoint,
        "MenMainWarCmn_Top_animjoint", &mnDataDel_804A0938.matanim_joint,
        "MenMainWarCmn_Top_matanim_joint", &mnDataDel_804A0938.shapeanim_joint,
        "MenMainWarCmn_Top_shapeanim_joint", 0);''', 1),
    ],
    'melee/mn/mninfo.c': [
        # The 32-char name's NUL was the first byte of the following gap
        # object (+0xD8), which ARM does not place after 803EFC08.
        ('''                           &model->shapeanim_joint,
                           layout->top_shapeanim_joint, 0);''',
         '''                           &model->shapeanim_joint,
                           /* Its NUL was the next object in the matching
                            * build, past 803EFC08; ARM does not keep that. */
                           "MenMainConCo_Top_shapeanim_joint", 0);''', 1),
    ],
    'melee/mn/mncount.c': [
        # The minutes were formatted into the stack slot 4 bytes below buf.
        ('''        char buf[4];
        text->font_size.x = 0.03f;''', '''        char buf[4];
        /* The matching frame had a second buffer 4 bytes below buf; ARM
         * stack layout does not. Give the minutes their own buffer. */
        char min_buf[4];
        text->font_size.x = 0.03f;''', 1),
        ('mn_8022EA78(buf - 4, 2, row_value / 60 % 60);',
         'mn_8022EA78(min_buf, 2, row_value / 60 % 60);', 1),
        ('(char*) buf - 4);', 'min_buf);', 1),
    ],
}
