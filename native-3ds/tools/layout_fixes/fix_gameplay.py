"""Gameplay (screen shake, Mute City cars, fighter init, Link arrows, trophy BG)."""

FIXES = {
    'melee/cm/camera.c': [
        # Camera_ApplyQuake: desc at +0x4C is cm_803BCB64, past the 0x24-byte
        # callbacks. The out-of-bounds reads let LLVM drop the quake entirely.
        ('''    struct CameraStaticData {
        CameraModeCallbacks callbacks;
        HSD_WObjDesc interest;
        HSD_WObjDesc eyepos;
        HSD_CameraDescPerspective desc;
    }* data = (struct CameraStaticData*) &cm_803BCB18;
''', '''    /* The matching build placed cm_803BCB18..cm_803BCB64 contiguously
     * (desc at +0x4C); ARM does not. Name the object. */
    HSD_CameraDescPerspective* desc = &cm_803BCB64;
''', 1),
        ('data->desc.', 'desc->', 5),
    ],
    'melee/gr/grmutecity.c': [
        # grMuteCity_801F106C: cars at +0x78 is grMc_8049F4B8, past the
        # s32[30] order array; ARM places it elsewhere (before it).
        ('''    typedef struct grMc_CarState {
        s32 idx[30];
        grMc_CarEntry cars[30];
    } grMc_CarState;
    f32 max_x8;
    grMc_CarState* state = (grMc_CarState*) grMc_8049F440;
    grMc_CarEntry* cars = state->cars;
    u16 flags16 = state->cars[i].x20;
''', '''    /* The matching build placed grMc_8049F4B8 right after grMc_8049F440
     * (s32[30]); ARM does not. Name the object. */
    f32 max_x8;
    grMc_CarEntry* cars = grMc_8049F4B8;
    u16 flags16 = cars[i].x20;
''', 1),
        ('state->cars[i]', 'cars[i]', 30),
    ],
    'melee/ft/ftdata.c': [
        # ft_800852B0: gFtDataList + 0x84 is ft_8045993C.
        ('''    ((ft_8045993C_t*) &list[Ft_Kind_Max])[i].pad_x0 = 0;
    ((ft_8045993C_t*) &list[Ft_Kind_Max])[i].x6_b0 = 0;
    ((ft_8045993C_t*) &list[Ft_Kind_Max])[i].x6_b1_b2 = 0;
''', '''    /* The matching build placed ft_8045993C right after gFtDataList;
     * ARM does not. Name the object. */
    ft_8045993C[i].pad_x0 = 0;
    ft_8045993C[i].x6_b0 = 0;
    ft_8045993C[i].x6_b1_b2 = 0;
''', 1),
        # CostumeListsForeachCharacter + 0x108 / + 0x1734 are
        # ftData_Table_Unk0 / ftData_UnkIntPairs (declared in ftdata.h).
        ('''    ftData_UnkCountStruct* unk0 =
        (ftData_UnkCountStruct*) &CostumeListsForeachCharacter[Ft_Kind_Max];
    ftData_UnkCountStruct* pairs =
        (ftData_UnkCountStruct*) ((u8*) CostumeListsForeachCharacter + 5940);
''', '''    /* The matching build placed ftData_Table_Unk0 (+0x108) and
     * ftData_UnkIntPairs (+0x1734) after CostumeListsForeachCharacter;
     * ARM does not. Name the objects. */
    ftData_UnkCountStruct* unk0 = ftData_Table_Unk0;
    ftData_UnkCountStruct* pairs = ftData_UnkIntPairs;
''', 1),
    ],
    'melee/it/kinds/itlinkarrow.c': [
        # itLinkarrow_UnkMotion4_Anim: it_803F6A28 (0x50 bytes) + 0x5C is
        # it_803F6A84; [23]/[31] are [0]/[8], as in itLinkArrow_802A81C4.
        ('''        temp_r3 = (f32*) &it_803F6A28 + ip->xDD4_itemVar.linkarrow.x9C;
        var_f32 = MTXDegToRad((temp_r3[31] * rand) + temp_r3[23]);
''', '''        /* The matching build placed it_803F6A84 at it_803F6A28 + 0x5C;
         * ARM does not. Name the object. */
        temp_r3 = &it_803F6A84[ip->xDD4_itemVar.linkarrow.x9C];
        var_f32 = MTXDegToRad((temp_r3[8] * rand) + temp_r3[0]);
''', 2),
    ],
    'melee/ty/toy.c': [
        # Toy_80310324: sym + 4 is 12 bytes past the 4-byte local, a spare
        # PPC stack slot. The joint is never read here (Toy_80307470 looks it
        # up from tg->x50), so the one-slot local is the storage it needs.
        ('''            sym + 4, _Toy_803FDEA0[0], NULL);
''', '''            /* sym + 4 was a spare slot in the matching build's stack
             * frame; ARM has none. The result is unused; store it in sym. */
            sym, _Toy_803FDEA0[0], NULL);
''', 1),
    ],
}
