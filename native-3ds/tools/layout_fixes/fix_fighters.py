"""Fighter/stage/item code reaching data through a neighbouring global or
stack slot (Kirby Stone slide normal, Rollout squash tables, sqrtf spills,
assert/report strings)."""

FIXES = {
    # 0x803CB490 + 0x74 = 0x803CB504 = ftKb_Init_803CB4EC + 0x18 (.vec, {0,1,0}).
    'melee/ft/kinds/ftKirby/ftkirbyspeciallw.c': [
        ('''    struct ftKb_Init_803CB490_layout* p =
        (struct ftKb_Init_803CB490_layout*) ftKb_Init_803CB490;
''',
         '''    /* PPC +0x74 past ftKb_Init_803CB490 is ftKb_Init_803CB4EC.vec */
    struct ftKb_Init_803CB4EC_t* p = &ftKb_Init_803CB4EC;
''', 8),
    ],
    # 0x803CB710 + 0x10 = ftKb_Init_803CB720.
    'melee/ft/kinds/ftKirby/ftkirbyspecialpurin.c': [
        ('scale->z = fp->u.kb.x8C.z * scale_base[frame + 4];',
         'scale->z = fp->u.kb.x8C.z * ftKb_Init_803CB720[frame]; /* PPC: scale_base + 4 */', 1),
    ],
    # 0x803D05C8 + 0x10 = ftPr_Init_803D05D8; + 0x20 = ftPr_Init_assert_msg_0.
    'melee/ft/kinds/ftPurin/ftpurinspecialn.c': [
        ('scale->z = fp->u.pr.x2230.z * scale_base[frame + 4];',
         'scale->z = fp->u.pr.x2230.z * ftPr_Init_803D05D8[frame]; /* PPC: scale_base + 4 */', 1),
        ('__assert("jobj.h", 661, (char*) &base[8])',
         '__assert("jobj.h", 661, "!(jobj->flags & JOBJ_USE_QUATERNION)") /* PPC: base + 0x20 */', 1),
    ],
    # sqrtf spills below the scratch array on PPC; any valid slot will do.
    'melee/mp/mplib.c': [
        ('sqrtf_store(x_f2 + y_f0, sqrt_tmp - 4)',
         'sqrtf_store(x_f2 + y_f0, &sqrt_tmp[0]) /* PPC: sqrt_tmp - 4 */', 1),
        ('sqrtf_store(x_f2 + y_f0, sqrt_tmp - 5)',
         'sqrtf_store(x_f2 + y_f0, &sqrt_tmp[1]) /* PPC: sqrt_tmp - 5 */', 1),
    ],
    'melee/lb/lbcollision.c': [
        ('sqrtf_store(n1, sqrt_tmp - 1)',
         'sqrtf_store(n1, &sqrt_tmp[1]) /* PPC: sqrt_tmp - 1 */', 1),
    ],
    # Dormant unless MUST_MATCH (macro drops the slot); keep it in bounds anyway.
    'melee/ft/ftcpuattack.c': [
        ('sqrtf_store(v, sqrt_tmp - 1)',
         'sqrtf_store(v, &sqrt_tmp[3]) /* PPC: sqrt_tmp - 1 */', 2),
    ],
    'melee/it/kinds/itlinkhookshot.c': [
        ('''        *(&y + 6) = (f32) (x * guess);
        return *(&y + 6);
''',
         '''        /* PPC spill 6 words past y; the slot itself is scratch */
        y = (f32) (x * guess);
        return y;
''', 1),
    ],
    # 0x803E7638 + 0xCC = @260.
    'melee/gr/grgreens.c': [
        ('OSReport((char*) grGr_callbacks + 0xCC, "grgreens.c", 281, id);',
         'OSReport("%s:%d: couldn t get gobj(id=%d)\\n", "grgreens.c", 281, id); /* PPC: grGr_callbacks + 0xCC */', 1),
    ],
}
