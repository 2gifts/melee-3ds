"""VS Records screens (mnDiagram pages 1-3)."""

FIXES = {
    'melee/mn/mndiagram.c': [
        # tbl->intro_anim (+0x40) is 803EE768, arrow/cursor_anim (+0x58/+0x64)
        # are PopupExitAnimFrames[3]/[6], +0x70/+0x88/+0x94 the assert strings.
        ('#define GET_DIAGRAM_ANIM_TABLE() ((mnDiagram_AnimTable*) &mnDiagram_803EE728)\n',
         '''#define GET_DIAGRAM_ANIM_TABLE() ((mnDiagram_AnimTable*) &mnDiagram_803EE728)
/* The matching build placed 803EE768, PopupExitAnimFrames and the assert
 * strings right after 803EE728. ARM does not; only tbl->points is in bounds. */
#define mnDiagram_ArrowAnim ((AnimLoopSettings*) &mnDiagram_PopupExitAnimFrames[3])
#define mnDiagram_CursorAnim ((AnimLoopSettings*) &mnDiagram_PopupExitAnimFrames[6])
''', 1),
        ('&tbl->arrow_anim', 'mnDiagram_ArrowAnim', 4),
        ('&tbl->cursor_anim', 'mnDiagram_CursorAnim', 1),
        ('tbl->cursor_anim.end_frame', 'mnDiagram_CursorAnim->end_frame', 1),
        ('tbl->intro_anim.end_frame', 'mnDiagram_803EE768.end_frame', 1),
        ('''        OSReport(tbl->user_data_error);
        __assert(tbl->file_name, 0x5F8, tbl->user_data_name);''',
         '''        /* @2124/@2125/@2126, contiguous after 803EE728 only in the
         * matching build. */
        OSReport("Can't get user_data.\\n");
        __assert("mndiagram.c", 0x5F8, "user_data");''', 1),
        # mnDiagram_Assets over 804A0750: +0x1C is 804A076C, +0x94.. the
        # asset blocks 804A07E4..804A0854. On ARM the stores clobber others.
        ('    mnDiagram_Assets* assets = (mnDiagram_Assets*) &mnDiagram_804A0750;\n',
         '''    /* The matching build placed 804A076C and the asset blocks right after
     * 804A0750 (mnDiagram_Assets). ARM does not; name the objects. */
''', 3),
        ('    u8* dst = assets->sorted_names;',
         '    u8* dst = mnDiagram_804A076C.sorted_names;', 1),
        ('''            u8* p = &assets->sorted_fighters[max_idx];
            u8 temp = *(p += sizeof(mnDiagram_804A0750_t));''',
         '''            u8* p = &mnDiagram_804A076C.sorted_names[max_idx];
            u8 temp = *p;''', 1),
        ('joint_data = assets->FaceB;', 'joint_data = mnDiagram_804A0804;', 2),
        ('''            archive, &assets->ConB1[0], "MenMainConB1_Top_joint",
            &assets->ConB1[1], "MenMainConB1_Top_animjoint", &assets->ConB1[2],
            "MenMainConB1_Top_matanim_joint", &assets->ConB1[3],
            "MenMainConB1_Top_shapeanim_joint", &assets->CursorB1[0],
            "MenMainCursorB1_Top_joint", &assets->FaceB[0],
            "MenMainFaceB_Top_joint", &assets->FaceB[1],
            "MenMainFaceB_Top_animjoint", &assets->FaceB[2],
            "MenMainFaceB_Top_matanim_joint", &assets->FaceB[3],
            "MenMainFaceB_Top_shapeanim_joint", &assets->NmB[0],
            "MenMainNmB_Top_joint", &assets->NmB[1],
            "MenMainNmB_Top_animjoint", &assets->NmB[2],
            "MenMainNmB_Top_matanim_joint", &assets->NmB[3],
            "MenMainNmB_Top_shapeanim_joint", &assets->SubB1[0],
            "MenMainSubB1_Top_joint", &assets->SubB1[1],
            "MenMainSubB1_Top_animjoint", &assets->SubB1[2],
            "MenMainSubB1_Top_matanim_joint", &assets->SubB1[3],
            "MenMainSubB1_Top_shapeanim_joint", &assets->ConB2[0],
            "MenMainConB2_Top_joint", &assets->ConB2[1],
            "MenMainConB2_Top_animjoint", &assets->ConB2[2],
            "MenMainConB2_Top_matanim_joint", &assets->ConB2[3],
            "MenMainConB2_Top_shapeanim_joint", &assets->ConB3[0],
            "MenMainConB3_Top_joint", &assets->ConB3[1],
            "MenMainConB3_Top_animjoint", &assets->ConB3[2],
            "MenMainConB3_Top_matanim_joint", &assets->ConB3[3],
            "MenMainConB3_Top_shapeanim_joint", &assets->CursorB3[0],
            "MenMainCursorB3_Top_joint", 0);''',
         '''            archive, &mnDiagram_804A0824[0], "MenMainConB1_Top_joint",
            &mnDiagram_804A0824[1], "MenMainConB1_Top_animjoint",
            &mnDiagram_804A0824[2], "MenMainConB1_Top_matanim_joint",
            &mnDiagram_804A0824[3], "MenMainConB1_Top_shapeanim_joint",
            &mnDiagram_804A0814[0], "MenMainCursorB1_Top_joint",
            &mnDiagram_804A0804[0], "MenMainFaceB_Top_joint",
            &mnDiagram_804A0804[1], "MenMainFaceB_Top_animjoint",
            &mnDiagram_804A0804[2], "MenMainFaceB_Top_matanim_joint",
            &mnDiagram_804A0804[3], "MenMainFaceB_Top_shapeanim_joint",
            &mnDiagram_804A07F4[0], "MenMainNmB_Top_joint",
            &mnDiagram_804A07F4[1], "MenMainNmB_Top_animjoint",
            &mnDiagram_804A07F4[2], "MenMainNmB_Top_matanim_joint",
            &mnDiagram_804A07F4[3], "MenMainNmB_Top_shapeanim_joint",
            &mnDiagram_804A07E4[0], "MenMainSubB1_Top_joint",
            &mnDiagram_804A07E4[1], "MenMainSubB1_Top_animjoint",
            &mnDiagram_804A07E4[2], "MenMainSubB1_Top_matanim_joint",
            &mnDiagram_804A07E4[3], "MenMainSubB1_Top_shapeanim_joint",
            &mnDiagram_804A0834.x0, "MenMainConB2_Top_joint",
            &mnDiagram_804A0834.x4, "MenMainConB2_Top_animjoint",
            &mnDiagram_804A0834.x8, "MenMainConB2_Top_matanim_joint",
            &mnDiagram_804A0834.xC, "MenMainConB2_Top_shapeanim_joint",
            &mnDiagram_804A0844.x0, "MenMainConB3_Top_joint",
            &mnDiagram_804A0844.x4, "MenMainConB3_Top_animjoint",
            &mnDiagram_804A0844.x8, "MenMainConB3_Top_matanim_joint",
            &mnDiagram_804A0844.xC, "MenMainConB3_Top_shapeanim_joint",
            &mnDiagram_804A0854.x0, "MenMainCursorB3_Top_joint", 0);''', 1),
        # sorted_fighters + 0x1C (a fighter-list pointer) was 804A076C.
        ('    p = p + 0x1C;\n',
         '    /* was sorted + 0x1C: 804A076C, contiguous only in the matching build */\n'
         '    p = mnDiagram_804A076C.sorted_names + start;\n', 3),
        ('    return p[0x1C];', '    return mnDiagram_804A076C.sorted_names[idx];', 3),
        ('''        ptr = sorted + i;
        ptr = ptr + 0x1C;''',
         '''        /* was sorted + 0x1C: 804A076C, contiguous only in the matching build */
        ptr = mnDiagram_804A076C.sorted_names + i;''', 1),
        ('''        ptr = sorted;
        ptr += i;
        result = ptr[0x1C];''',
         '        result = mnDiagram_804A076C.sorted_names[i];', 1),
    ],
    'melee/mn/mndiagram2.c': [
        # base->anim (+0x90) is 803EEB60.
        ('    base = (MnDiagram2DataLayout*) &mnDiagram2_803EEAD0;\n',
         '''    /* The matching build placed 803EEB60 right after 803EEAD0 (the old
     * base->anim at +0x90). ARM does not; name the object. */
    base = NULL;
''', 1),
        ('&base->anim[1]', '&mnDiagram2_803EEB60[1]', 4),
    ],
    'melee/mn/mndiagram3.c': [
        # DataTable over 803EEC10: positions (+0x18) is 803EEC28, stats
        # (+0x3C) is 803EEC4C.
        ('    table = (mnDiagram3_DataTable*) &mnDiagram3_803EEC10;\n',
         '''    /* The matching build placed 803EEC28/803EEC4C right after 803EEC10
     * (mnDiagram3_DataTable). ARM does not; name the objects. */
    table = NULL;
''', 1),
        ('table->stats.unit_glyph_ids', 'mnDiagram3_803EEC4C.unit_glyph_ids', 1),
        ('&table->positions.xC', '&mnDiagram3_803EEC28.xC', 1),
        ('&table->positions.x18', '&mnDiagram3_803EEC28.x18', 2),
        ('    lb_8000B1CC(data->jobjs[8], (Vec3*) (base + 0x18), pos);',
         '''    /* base + 0x18/+0x3C were 803EEC28/803EEC4C, contiguous after 803EEC10
     * only in the matching build. */
    lb_8000B1CC(data->jobjs[8], &mnDiagram3_803EEC28.x0, pos);''', 1),
        ('((u16*) (base + 0x3C))[', 'mnDiagram3_803EEC4C.label_ids[', 1),
    ],
}
