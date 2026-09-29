"""Snapshot JPEG codec (Album save/view), MCC host-IO and refraction setup."""

FIXES = {
    # JPEG decode. PPC: jmp_buf hsd_804D2E70 (0x118) is followed by the 0x70C
    # work area hsd_804D2F68 (at 804D2F88); the decoder reaches it at +0x118
    # (+0x818 prev_dc). newlib's ARM jmp_buf is 0xA0, so those offsets ran
    # past the object and disagreed with JpegState.work (+0xA0) used by the
    # IDCT/colour paths. One JpegState; every access by field.
    'sysdolphin/baselib/hsd_3B5C.c': [
        ('jmp_buf hsd_804D2E70;\nu8 hsd_804D2F68[0x70C];\n', '', 1),
        ('    JpegWorkData work;\n} JpegState;\n',
         '    JpegWorkData work;\n} JpegState;\n\n'
         '/* PPC 804D2E70 jmp_buf + 804D2F88 work, as one object. */\n'
         'JpegState hsd_804D2E70;\n', 1),
        ('longjmp(hsd_804D2E70, 1);', 'longjmp(hsd_804D2E70.jmp, 1);', 3),
        ('((s32*) &base[0x818])', '((JpegState*) base)->work.prev_dc', 2),
        ('((JpegWorkData*) &base[0x118])', '(&((JpegState*) base)->work)', 5),
    ],
    # JPEG encode. (a) lbl_80430C40 (luma quant, 0x40) directly precedes
    # lbl_80430C80 (0x410): JpegEncodeTables and quant_table + 0x40 read the
    # chroma quant and AC Huffman tables past the luma table. One array.
    # (b) jpegLumaAddress and the block loop hard-coded the PPC jmp_buf size
    # (+0x118/+0x518/+0x618/+0x718); ARM's is 0xA0, which overran
    # hsd_804D2648 and missed the x518/x618/coef fields hsd_803B3408 and
    # hsd_803B3CD8 use. Name the fields.
    'sysdolphin/baselib/hsd_3B34.c': [
        ('static u8 lbl_80430C40[0x40] = {\n',
         '/* PPC 80430C40 (0x40) + 80430C80 (0x410), as one object. */\n'
         'static u8 lbl_80430C40[0x40 + 0x410] __attribute__((aligned(4))) = {\n', 1),
        ('};\nstatic u8 lbl_80430C80[0x410] = {\n', '    /* lbl_80430C80 */\n', 1),
        ('0x00, 0x00,\n};\nextern u8 lbl_80431638[0x40];\n',
         '0x00, 0x00,\n};\n#define lbl_80430C80 (lbl_80430C40 + 0x40)\n'
         'extern u8 lbl_80431638[0x40];\n', 1),
        ('    u8 pad_44E[2];\n} JpegEncodeTables;\n',
         '    u8 pad_44E[2];\n} JpegEncodeTables;\n'
         '_Static_assert(sizeof(JpegEncodeTables) == sizeof(lbl_80430C40),\n'
         '               "encode tables are one object");\n', 1),
        ('    work += 0x118;\n    *dest = &((s32*) work)[offset / 4];\n',
         '    *dest = &((JpegWork*) work)->x118[offset / 4];\n', 1),
        ('            work_r23 = state.base + ((work_r26 = 0) << 8);\n'
         '            work_r23 += 0x118;\n',
         '            work_r26 = 0;\n'
         '            work_r23 = (u8*) state.work->x118;\n', 1),
        ('(s32*) (state.base + 0x718)', 'state.work->coef', 2),
        ('fn_803B376C(state.base + 0x518)', 'fn_803B376C((u8*) state.work->x518)', 1),
        ('(s32*) (state.base + 0x518)', 'state.work->x518', 1),
        ('fn_803B376C(state.base + 0x618)', 'fn_803B376C((u8*) state.work->x618)', 1),
        ('(s32*) (state.base + 0x618)', 'state.work->x618', 1),
    ],
    # MCC host-IO: hsd_804CEB40 (0x100 log entries, 0xC00) directly precedes
    # hsd_804CF740. base[0x100] is its channel flags, base[0x105].x4 (+0x40)
    # the response packet, base[0x10A].x8 (+0x80) the request packet.
    'sysdolphin/baselib/hsd_3933.c': [
        ('((s32*) &base[0x100])', 'hsd_804CF740', 4),
        ('&base[0x105].x4', '&hsd_804CF740[16]', 5),
        ('&base[0x10A].x8', '&hsd_804CF740[32]', 6),
    ],
    # texture_mtx (0x30) + texture_offset (0x18) precede imagedesc0 in PPC
    # .data; the +0x48 layout field is imagedesc0 itself.
    'melee/lb/lbrefract.c': [
        ('    /// @todo Refactor data members into a struct\n'
         '    struct lbRefract_DataLayout {\n'
         '        Mtx texture_mtx;\n'
         '        f32 texture_offset[6];\n'
         '        HSD_ImageDesc imagedesc0;\n'
         '    };\n\n'
         '    lbRefract_CallbackData cb;\n'
         '    struct lbRefract_DataLayout* data =\n'
         '        (struct lbRefract_DataLayout*) &texture_mtx;\n',
         '    lbRefract_CallbackData cb;\n', 1),
        ('        lbl_804336D0.imagedesc[i] = data->imagedesc0;\n',
         '        lbl_804336D0.imagedesc[i] = imagedesc0;\n', 1),
    ],
}
