"""Slippi determinism: source edits that make the engine's floating point
round like the GameCube (docs/slippi/determinism.md).

Two parts:
* HAND: edits written for this port (console maths library enablement).
* PORTED: Melee Unlocked's float annotations (MU_P(...) product wraps and
  explicit fused operations), carried over from
  sourceport/patches/melee-native.patch (GPL-3.0-or-later) by
  tools/slippi/port_mu_annotations.py into determinism_ported.py.

Function-level "the console fused nothing here" tags are not source edits:
they live in tools/slippi/no_contract.txt and act on the IR
(tools/fp_contract.py).
"""

HAND = {
    # MSL's sinf/cosf read __four_over_pi_m1, which the console fills from
    # a static constructor (__sinit_trigf_c in .ctors). The port runs no
    # .ctors for the engine: initialise it statically with the same floats.
    'MSL/trigf.c': [
        ('f32 __four_over_pi_m1[] = { 0.0f, 0.0f, 0.0f, 0.0f };',
         'f32 __four_over_pi_m1[] = { 0.25f, 0.0232393741608f, 1.70555722434e-7f,\n'
         '                            1.86736494323e-11f }; /* __sinit_trigf_c */', 1),
    ],
    # Melee's own atanf (the decomp builds it for MWCC only).
    'melee/lb/lbtrigf.c': [
        ('#ifdef __MWERKS__\nfloat atanf(float x)', '#if 1 /* Slippi: the console atanf */\nfloat atanf(float x)', 1),
    ],

    # ---- Melee Unlocked annotations the script could not carry over (they
    # were mixed with x86 adaptations); hand-ported, same intent. Explicit
    # fused operations below are ones clang would also form by itself
    # (a -= b*c and a = b*c + a contract within the expression); spelling
    # them out keeps them fused whatever the contraction rules do.
    'melee/cm/camera.c': [
        ('    return sqrtf((offset->x * offset->x) + (offset->y * offset->y) +\n'
         '                 (offset->z * offset->z));',
         '    return sqrtf(MU_P(offset->x * offset->x) + MU_P(offset->y * offset->y) +\n'
         '                 MU_P(offset->z * offset->z));', 1),
    ],
    'melee/ft/fighter.c': [
        # fnmsubs on the console (8006b9c8, 8006b9e4)
        ('                        p_kb_vel->x -=\n'
         '                            p_ftCommonData->x204_knockbackFrameDecay *\n'
         '                            cosf(kb_angle);\n'
         '                        p_kb_vel->y -=\n'
         '                            p_ftCommonData->x204_knockbackFrameDecay *\n'
         '                            sinf(kb_angle);',
         '                        p_kb_vel->x = mu_fnmsubs(\n'
         '                            p_ftCommonData->x204_knockbackFrameDecay,\n'
         '                            cosf(kb_angle), p_kb_vel->x);\n'
         '                        p_kb_vel->y = mu_fnmsubs(\n'
         '                            p_ftCommonData->x204_knockbackFrameDecay,\n'
         '                            sinf(kb_angle), p_kb_vel->y);', 1),
        # fnmsubs on the console (8006bb34, 8006bb50)
        ('                    pAtkShieldKB->x -=\n'
         '                        p_ftCommonData->x3E8_shieldKnockbackFrameDecay *\n'
         '                        cosf(atkShieldKBAngle);\n'
         '                    pAtkShieldKB->y -=\n'
         '                        p_ftCommonData->x3E8_shieldKnockbackFrameDecay *\n'
         '                        sinf(atkShieldKBAngle);',
         '                    pAtkShieldKB->x = mu_fnmsubs(\n'
         '                        p_ftCommonData->x3E8_shieldKnockbackFrameDecay,\n'
         '                        cosf(atkShieldKBAngle), pAtkShieldKB->x);\n'
         '                    pAtkShieldKB->y = mu_fnmsubs(\n'
         '                        p_ftCommonData->x3E8_shieldKnockbackFrameDecay,\n'
         '                        sinf(atkShieldKBAngle), pAtkShieldKB->y);', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_Damage.c': [
        ('            SQ(p_ftCommonData->sdi_min_stick_mag) &&',
         '            MU_P(SQ(p_ftCommonData->sdi_min_stick_mag)) &&', 1),
        # Census: ftCo_Damage_OnExitHitlag fuses only the knockback length
        # (0x8008E860); the stick's squares come from memory and are not.
        ('    if (VEC2_SQ_LEN(fp->input.lstick[0]) >=\n'
         '        SQ(p_ftCommonData->sdi_min_stick_mag))\n'
         '    {\n'
         '        isPointInCircle = true;',
         '    if (MU_P(SQ(fp->input.lstick[0].x)) + MU_P(SQ(fp->input.lstick[0].y)) >=\n'
         '        SQ(p_ftCommonData->sdi_min_stick_mag))\n'
         '    {\n'
         '        isPointInCircle = true;', 1),
    ],
    # Census: the grab/bury/sing escape timers end in `+ k * (n - port)`;
    # the console computes that product before the handicap call and keeps it
    # across the call, so it is added unfused (0x800C0DC4 fmuls, 0x800C0E08
    # fadds). clang would fuse it as the right operand of the sum.
    'melee/ft/kinds/ftCommon/ftCo_Bury.c': [
        ('             (p_ftCommonData->x604 *\n'
         '              (p_ftCommonData->x608 - (Player_80033BB8(fp->player_id) + 1)))));',
         '             MU_P(p_ftCommonData->x604 *\n'
         '              (p_ftCommonData->x608 - (Player_80033BB8(fp->player_id) + 1)))));', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_DamageSong.c': [
        ('         p_ftCommonData->x630 *\n'
         '             (p_ftCommonData->x634 - ((Player_80033BB8(fp->player_id)) + 1)));',
         '         MU_P(p_ftCommonData->x630 *\n'
         '             (p_ftCommonData->x634 - ((Player_80033BB8(fp->player_id)) + 1))));', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_DamageBind.c': [
        ('             p_ftCommonData->x664 * (p_ftCommonData->pressed_inputs -\n'
         '                                     (Player_80033BB8(fp->player_id) + 1))));',
         '             MU_P(p_ftCommonData->x664 * (p_ftCommonData->pressed_inputs -\n'
         '                                     (Player_80033BB8(fp->player_id) + 1)))));', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_Guard.c': [
        # Melee Unlocked (ftCo_800925A4): the console rounds the shield drain
        # product before the subtraction (0x80092628 fmuls, then fsubs).
        ('        fp->shield_health -= p_ftCommonData->x278 *\n'
         '                             ((fp->lightshield_amount *\n'
         '                               (p_ftCommonData->x2F0 - p_ftCommonData->x2EC)) +\n'
         '                              p_ftCommonData->x2EC);',
         '        fp->shield_health -= MU_P(p_ftCommonData->x278 *\n'
         '                             ((fp->lightshield_amount *\n'
         '                               (p_ftCommonData->x2F0 - p_ftCommonData->x2EC)) +\n'
         '                              p_ftCommonData->x2EC));', 1),
    ],
    'melee/it/kinds/itlinkhookshot.c': [
        # Melee Unlocked: the console inlines it_802A3C98 into every caller in
        # this file, where the fresh differences sit in registers and the
        # squares fuse; the out-of-line copy (no_contract.txt) does not.
        ('        inv = (f64) 1.0F / len;\n    }\n    arg2->x *= inv;\n    arg2->y *= inv;\n'
         '    arg2->z *= inv;\n    return len;\n}\n',
         '        inv = (f64) 1.0F / len;\n    }\n    arg2->x *= inv;\n    arg2->y *= inv;\n'
         '    arg2->z *= inv;\n    return len;\n}\n'
         '/* Slippi determinism: the inlined copy the console uses in this file. */\n'
         'static inline float mp_it_802A3C98_inl(Vec3* arg0, Vec3* arg1, Vec3* arg2)\n{\n'
         '    f32 inv;\n    f32 len;\n'
         '    arg2->x = arg0->x - arg1->x;\n    arg2->y = arg0->y - arg1->y;\n    arg2->z = arg0->z - arg1->z;\n'
         '    len = sqrtf(arg2->x * arg2->x + arg2->y * arg2->y + arg2->z * arg2->z);\n'
         '    if (len == (f64) 0.0F) {\n        inv = (f64) 0.0F;\n    } else {\n'
         '        inv = (f64) 1.0F / len;\n    }\n'
         '    arg2->x *= inv;\n    arg2->y *= inv;\n    arg2->z *= inv;\n    return len;\n}\n'
         '#define it_802A3C98(a0, a1, a2) mp_it_802A3C98_inl(a0, a1, a2)\n', 1),
    ],
    'melee/it/itmaplib.c': [
        # Census (it_8027781C): the speed's squares come from memory and the
        # console does not fuse them (0x80277894 fmuls x2, fadds).
        ('    return sqrtf_accurate_local(product_xy(v, v));',
         '    return sqrtf_accurate_local(MU_P(v->x * v->x) + MU_P(v->y * v->y));', 1),
    ],
    'sysdolphin/baselib/quatlib.c': [
        # Census (HSD_QuatLib_8037EF28, quaternion slerp): the console
        # computes 2*t once for both sinf arguments (0x8037F080 fmuls) and
        # subtracts it unfused.
        ('            sp = sinf((f32) (M_PI_2 * (1.0F - (2.0F * t))));',
         '            sp = sinf((f32) (M_PI_2 * (1.0F - MU_P(2.0F * t))));', 1),
    ],
    'sysdolphin/baselib/spline.c': [
        # Melee Unlocked: one p1 multiply, then fmadds for p0, d0, d1 (the
        # order clang's contraction also gives; spelled out to keep it).
        ('    return (d1 * (t3_T2 - t2_T)) + ((d0 * (time + ((t3_T2 - t2_T) - t2_T))) +\n'
         '                                    ((p0 * (1.0f + (_2t3_T3 - _3t2_T2))) +\n'
         '                                     (p1 * (-_2t3_T3 + _3t2_T2))));',
         '    {\n'
         '        f32 value = p1 * (-_2t3_T3 + _3t2_T2);\n'
         '        value = mu_fmadds(p0, 1.0f + (_2t3_T3 - _3t2_T2), value);\n'
         '        value = mu_fmadds(d0, time + ((t3_T2 - t2_T) - t2_T), value);\n'
         '        return mu_fmadds(d1, t3_T2 - t2_T, value);\n'
         '    }', 1),
    ],
    'melee/ft/kinds/ftPurin/ftpurinspecialn.c': [
        # fmadds on the console
        ('            fp->gr_vel += scale * (x1C + influence);',
         '            fp->gr_vel = MU_FMADDS(scale, x1C + influence, fp->gr_vel);', 2),
        ('            fp->gr_vel += scale * (x1C - influence);',
         '            fp->gr_vel = MU_FMADDS(scale, x1C - influence, fp->gr_vel);', 2),
    ],
    'melee/gr/grcastle.c': [
        ('                         sqrtf__Ff(\n'
         '                             (pos.x - target_pos.x) * (pos.x - target_pos.x) +\n'
         '                             (pos.y - target_pos.y) * (pos.y - target_pos.y)) <',
         '                         sqrtf__Ff(\n'
         '                             MU_P((pos.x - target_pos.x) * (pos.x - target_pos.x)) +\n'
         '                             MU_P((pos.y - target_pos.y) * (pos.y - target_pos.y))) <', 1),
    ],
    'melee/gr/grkongo.c': [
        ('    if (!((pos_gnd.x - pos_ft.x) * (pos_gnd.x - pos_ft.x) +\n'
         '              (pos_gnd.y - pos_ft.y) * (pos_gnd.y - pos_ft.y) +\n'
         '              (pos_gnd.z - pos_ft.z) * (pos_gnd.z - pos_ft.z) <\n'
         '          yakumono_param->unk28 * yakumono_param->unk28))',
         '    if (!(MU_P((pos_gnd.x - pos_ft.x) * (pos_gnd.x - pos_ft.x)) +\n'
         '              MU_P((pos_gnd.y - pos_ft.y) * (pos_gnd.y - pos_ft.y)) +\n'
         '              MU_P((pos_gnd.z - pos_ft.z) * (pos_gnd.z - pos_ft.z)) <\n'
         '          MU_P(yakumono_param->unk28 * yakumono_param->unk28)))', 1),
    ],
    'melee/gr/grmutecity.c': [
        ('    return SQ(a[0] - b[0]) + SQ(a[1] - b[1]) + SQ(a[2] - b[2]);',
         '    return MU_P(SQ(a[0] - b[0])) + MU_P(SQ(a[1] - b[1])) + MU_P(SQ(a[2] - b[2]));', 1),
    ],
    'melee/lb/lbcollision.c': [
        # fmadds on the console (80007134, 80007144)
        ('    hurt_len_sq = axis.x * axis.x + hurt_len_sq;\n'
         '    hurt_len_sq = axis.z * axis.z + hurt_len_sq;',
         '    hurt_len_sq = mu_fmadds(axis.x, axis.x, hurt_len_sq);\n'
         '    hurt_len_sq = mu_fmadds(axis.z, axis.z, hurt_len_sq);', 1),
        # Census (lbColl_800077A0): the console squares diff_cb, reloaded from
        # the stack, unfused (fmuls x3, fadds x2); diff_ba's squares fuse.
        ('        dot_diff_cb = diff_cb.x * diff_cb.x + diff_cb.y * diff_cb.y +\n'
         '                      diff_cb.z * diff_cb.z;',
         '        dot_diff_cb = MU_P(diff_cb.x * diff_cb.x) + MU_P(diff_cb.y * diff_cb.y) +\n'
         '                      MU_P(diff_cb.z * diff_cb.z);', 1),
    ],
    'melee/lb/lbvector.c': [
        # Census (tools/slippi/fma_census.py): lbVector_Len is unfused where
        # its vector comes from memory (lbvector.h, no_contract.txt), but in
        # lbVector_8000E838 the vector is a local held in registers and the
        # console fuses: y*y, then x*x + _, then z*z + _ (0x8000E8C0..).
        ('float lbVector_8000E838(Vec3* a, Vec3* b, Vec3* c, Vec3* d)\n{',
         '/* Slippi determinism: lbVector_Len with the console\'s fusion here. */\n'
         'static inline float mp_lbVector_LenFused(Vec3* vec)\n{\n'
         '    return sqrtf(vec->x * vec->x + vec->y * vec->y + vec->z * vec->z);\n}\n\n'
         'float lbVector_8000E838(Vec3* a, Vec3* b, Vec3* c, Vec3* d)\n{', 1),
        ('        return lbVector_Len(&c_a);', '        return mp_lbVector_LenFused(&c_a);', 1),
        ('        return lbVector_Len(&v3);', '        return mp_lbVector_LenFused(&v3);', 1),
        ('    return (a->x * b->x + a->y * b->y) / (sqrtf(a->x * a->x + a->y * a->y) *\n'
         '                                          sqrtf(b->x * b->x + b->y * b->y));',
         '    return (a->x * b->x + a->y * b->y) / (sqrtf(MU_P(a->x * a->x) + MU_P(a->y * a->y)) *\n'
         '                                          sqrtf(MU_P(b->x * b->x) + MU_P(b->y * b->y)));', 1),
    ],
}

# Decomp files compiled with HSD_JObjAdd{Rotation,Scale,Translation}* as
# macros (port/include/mp_jobj_fused.h, after Melee Unlocked), so that
# `field += a * b` fuses like the console's inlined copies. Chosen by the
# census (tools/slippi/fma_census.py with and without the macros): these
# files match the console only with them; in others (e.g. the shells,
# grrcruise.c) the console rounds the product first.
JOBJ_FUSED = (
    'melee/ft/kinds/ftCommon/ftCo_JumpAerial.c',
    'melee/ty/toy.c',
    'melee/mn/mnitemsw.c',
    'melee/mn/mnstagesw.c',
)

try:
    from slippi_edits.determinism_ported import PORTED
except ImportError:  # not generated yet
    PORTED = {}

FIXES = {}
for table in (HAND, PORTED):
    for key, edits in table.items():
        FIXES.setdefault(key, []).extend(edits)


def _check_unique_keys():
    """A repeated key in HAND would silently drop the earlier edits."""
    import ast
    from pathlib import Path
    for node in ast.walk(ast.parse(Path(__file__).read_text(encoding='utf-8'))):
        if isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', None) == 'HAND':
            keys = [k.value for k in node.value.keys]
            assert len(keys) == len(set(keys)), 'duplicate key in determinism.HAND'


_check_unique_keys()
