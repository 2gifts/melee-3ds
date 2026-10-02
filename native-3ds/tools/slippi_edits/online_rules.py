"""Slippi's gameplay-affecting Gecko codes (port/engine/slippi/rules.c).

Each edit reproduces one injection of the code set Slippi Dolphin always runs
("Required: General Codes", "Required: Slippi Recording", "Required: Slippi
Online"; references/slippi-ssbm-asm/netplay.json), at the decomp function the
injection address falls in. The call sites follow Melee Unlocked's
melee-native.patch (GPL-3.0-or-later) where it ports the same code. Every code
is gated by mp_slippi_rule(MP_SR_...), see port/include/slippi_rules.h.
UCF 0.84 is the port's own port/engine/ucf.c (hooks in offline_overlays.py).
docs/slippi/rules.md lists the codes and how each was checked.
"""
DECL = '#include <slippi_rules.h>\n'


def _decl(anchor, extra=''):
    return (anchor, DECL + extra + anchor, 1)


FIXES = {
    'melee/ft/fighter.c': [
        _decl('#include "fighter.h"\n', '#include <string.h>\n'),
        # Common/Initialize Player Data (80068eec): zero the new fighter block.
        ('    fp = HSD_ObjAlloc(&fighter_alloc_data);\n',
         '    fp = HSD_ObjAlloc(&fighter_alloc_data);\n'
         '    if (mp_slippi_rule(MP_SR_INIT_PLAYER)) {\n'
         '        memset(fp, 0, sizeof(Fighter));\n'
         '    }\n', 1),
        # Online/Core/BrawlOffscreenDamage (8006a880): the camera limits, not
        # the magnifier bubble, decide offscreen damage.
        ('                if (ifMagnify_802FC998(fp->player_id) &&\n'
         '                    (Player_GetMoreFlagsBit3(fp->player_id) != 0))\n',
         '                if ((mp_slippi_rule(MP_SR_OFFSCREEN)\n'
         '                         ? mp_slippi_offscreen_zone(fp)\n'
         '                         : ifMagnify_802FC998(fp->player_id)) &&\n'
         '                    (Player_GetMoreFlagsBit3(fp->player_id) != 0))\n', 1),
    ],
    'melee/ft/ft_0D31.c': [
        _decl('#include "ft_0D31.h"\n'),
        # FreezeDeadUpFallPhysics/InitHitVelocity (800d4c1c).
        ('            fp->mv.co.unk_deadup.x40 = data[4];\n'
         '            fp->mv.co.unk_deadup.x44 = 3;\n',
         '            fp->mv.co.unk_deadup.x40 = data[4];\n'
         '            fp->mv.co.unk_deadup.x44 = 3;\n'
         '            if (mp_slippi_rule(MP_SR_DEAD_UP_FALL)) {\n'
         '                mp_slippi_dead_up_init(fp, *(f32*) (data + 12),\n'
         '                                       *(f32*) (data + 15));\n'
         '            }\n', 1),
        # FreezeDeadUpFallPhysics/UpdateFallVelocity (800d4d68).
        ('        ftCommon_Fall(fp, *(float*) (ca + 0x34), *(float*) (ca + 0x38));\n'
         '        lbVector_Add(&fp->mv.co.unk_deadup.x5C, &fp->self_vel);\n',
         '        if (mp_slippi_rule(MP_SR_DEAD_UP_FALL)) {\n'
         '            mp_slippi_dead_up_fall(fp, *(float*) (ca + 0x34),\n'
         '                                   *(float*) (ca + 0x38));\n'
         '        } else {\n'
         '            ftCommon_Fall(fp, *(float*) (ca + 0x34), *(float*) (ca + 0x38));\n'
         '            lbVector_Add(&fp->mv.co.unk_deadup.x5C, &fp->self_vel);\n'
         '        }\n', 1),
    ],
    'melee/ft/ftdrawcommon.c': [
        _decl('#include "ftdrawcommon.h"\n'),
        # FreezeDeadUpFallPhysics/UpdateModelPos (80080e80): the model follows
        # the camera, the physics position does not.
        ('    MtxPtr matrix = HSD_CObjGetInvViewingMtxPtr(Camera_800310B8());\n',
         '    MtxPtr matrix = HSD_CObjGetInvViewingMtxPtr(Camera_800310B8());\n'
         '    if (mp_slippi_rule(MP_SR_DEAD_UP_FALL)) {\n'
         '        Vec3 model_pos;\n'
         '        PSMTXMultVec(matrix, (Vec3*) &old->mv.co.walk.fast_anim_frame,\n'
         '                     &model_pos);\n'
         '        HSD_JObjSetTranslate(jobj, &model_pos);\n'
         '        return;\n'
         '    }\n', 1),
    ],
    'melee/ft/ftlib.c': [
        _decl('#include "ftlib.h"\n'),
        # Online/Core/WhispyBlowDirFix (8008653c): a fighter in a dead motion
        # counts for neither side.
        ('            ftLib_800866DC(cur, &vec);\n'
         '            result += sgn(vec.x - v->x);\n',
         '            ftLib_800866DC(cur, &vec);\n'
         '            if (mp_slippi_rule(MP_SR_WHISPY) && cur_fp->motion_id <= 0xB) {\n'
         '                continue;\n'
         '            }\n'
         '            result += sgn(vec.x - v->x);\n', 1),
    ],
    'melee/ft/ftdynamics.c': [
        _decl('#include "ftdynamics.h"\n'),
        # Common/FastForward/DynamicsFix (8009e090), at the function's end.
        ('            HSD_JObjSetupMatrix(cur);\n'
         '            i++;\n'
         '            dyn++;\n'
         '        }\n'
         '    }\n'
         '}\n',
         '            HSD_JObjSetupMatrix(cur);\n'
         '            i++;\n'
         '            dyn++;\n'
         '        }\n'
         '    }\n'
         '    mp_slippi_dynamics_fix(fp);\n'
         '}\n', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_CaptureWait.c': [
        _decl('#include "ftCo_Attack100.h"\n'),
        # External/PreventWobbling, Init Wobble Count Air (800db880) and
        # Ground (800dbbd4).
        ('    ftCommon_8007E2F4(fp, 0x1FF);\n}\n\nvoid fn_800DB8A4',
         '    ftCommon_8007E2F4(fp, 0x1FF);\n    mp_slippi_wobble_reset(fp);\n}\n\nvoid fn_800DB8A4', 1),
        ('    ftCommon_8007E2F4(fp, 0x1FF);\n}\n\nstatic inline void fn_800DBBF8_noinline',
         '    ftCommon_8007E2F4(fp, 0x1FF);\n    mp_slippi_wobble_reset(fp);\n}\n\n'
         'static inline void fn_800DBBF8_noinline', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_Damage.c': [
        _decl('#include "ftCo_Damage.h"\n'),
        # External/PreventWobbling, Wobble Check (8008f090): only the copy of
        # inlineB2 in ftCo_8008EC90 that follows the input clear.
        ('static inline void inlineB4(Fighter_GObj* gobj)\n',
         'static inline void inlineB2_wobble(Fighter_GObj* gobj)\n'
         '{\n'
         '    Fighter* fp = gobj->user_data;\n'
         '    ftCo_800C8D00(gobj);\n'
         '    if (!mp_slippi_wobble_check(gobj)) {\n'
         '        if (fp->motion_id == 0xe0 || fp->motion_id == 0xe1) {\n'
         '            ftCo_800DC284(gobj);\n'
         '        }\n'
         '        if (fp->motion_id == 0xe3 || fp->motion_id == 0xe4) {\n'
         '            ftCo_800DC3A4(gobj);\n'
         '        }\n'
         '    }\n'
         '    if (ftCo_8008DA4C(\n'
         '            gobj, fp->dmg.x1860_element,\n'
         '            ftCo_8008D8E8(fp->dmg.kb_applied * p_ftCommonData->x154)))\n'
         '    {\n'
         '        ftCo_800C0408(gobj);\n'
         '    }\n'
         '    ftCommon_800804FC(fp);\n'
         '}\n\n'
         'static inline void inlineB4(Fighter_GObj* gobj)\n', 1),
        ('                    fp->input.pressed_buttons = fp->input.released_buttons = 0;\n'
         '                    inlineB2(gobj);\n',
         '                    fp->input.pressed_buttons = fp->input.released_buttons = 0;\n'
         '                    inlineB2_wobble(gobj);\n', 1),
    ],
    'melee/ft/kinds/ftCommon/ftCo_0A01.c': [
        _decl('#include "ftCo_0A01.h"\n'),
        # Common/NanaDeterminism (800ac5b8): the stick values a CPU (Nana) uses
        # to DI a throw start at 0 instead of whatever the registers held.
        ('    s8 stick_x;\n    s8 stick_y;\n',
         '    s8 stick_x = 0;\n    s8 stick_y = 0;\n', 1),
    ],
    'melee/ft/kinds/ftNana/ftnanaspecials.c': [
        _decl('#include <Runtime/platform.h>\n'),
        # External/FreezeGlitchFix (801239a8, nop): keep Nana's x1A5C.
        ('            nana_fp->x1A5C = NULL;\n            ret = true;\n',
         '            if (!mp_slippi_rule(MP_SR_FREEZE_GLITCH)) {\n'
         '                nana_fp->x1A5C = NULL;\n'
         '            }\n'
         '            ret = true;\n', 1),
    ],
    'melee/gm/gmvs.c': [
        _decl('#include "gmvs.h"\n'),
        # External/NeutralSpawn (8016e510), after the start position is stored.
        ('                Player_80032768(i, &sp18);\n'
         '                is_teams = controller.start.is_teams == true;\n',
         '                Player_80032768(i, &sp18);\n'
         '                mp_slippi_neutral_spawn(i, controller.start.is_teams);\n'
         '                is_teams = controller.start.is_teams == true;\n', 1),
        # Online/Core/InitOnlinePlay (8016e748): its per-match gameplay part.
        ('void fn_8016E730(StartMeleeData* arg0)\n{\n'
         '    HSD_GObj* temp_r30;\n    VsSceneController* r30;\n\n',
         'void fn_8016E730(StartMeleeData* arg0)\n{\n'
         '    HSD_GObj* temp_r30;\n    VsSceneController* r30;\n\n'
         '    mp_slippi_rules_match_start();\n', 1),
    ],
    'melee/if/if_2F6E.c': [
        _decl('#include "if_2F6E.h"\n'),
        # Online/Core/LGLExceededGameEnd (802f70c4): online timeouts only.
        ('    if (mr == OUTCOME_TIMEOUT) {\n'
         '        ifStatus_802F6EA4(0, a, b, c, arg0, NULL);\n',
         '    if (mr == OUTCOME_TIMEOUT) {\n'
         '        ifStatus_802F6EA4(mp_slippi_lgl_timeout_message(), a, b, c, arg0,\n'
         '                          NULL);\n', 1),
    ],
    'melee/gr/grlast.c': [
        _decl('#include "grlast.h"\n', '#include <sysdolphin/baselib/random.h>\n'),
        # Online/Core/Hacks/FD/DesyncProofBGTransformations (8021aae4): the
        # background think leaves the seed as it found it.
        ('    if (!gp->u.map.xC4_b1 && !gp->u.map.xC4_b0) {\n'
         '        grLast_8021B2E8(gobj);\n',
         '    if (!gp->u.map.xC4_b1 && !gp->u.map.xC4_b0) {\n'
         '        if (mp_slippi_rule(MP_SR_FD_BG)) {\n'
         '            u32 seed = *seed_ptr;\n'
         '            grLast_8021B2E8(gobj);\n'
         '            *seed_ptr = seed;\n'
         '        } else {\n'
         '            grLast_8021B2E8(gobj);\n'
         '        }\n', 1),
    ],
    'melee/gr/ground.c': [
        _decl('#include "ground.h"\n', '#include <string.h>\n'),
        # Common/Initialize Stage Data (801c154c): zero the 516-byte block
        # (Ground_GetStageGObj only, not Ground_801C1A20).
        ('    gp = alloc_user_data_ground();\n'
         '    if (gp == NULL) {\n'
         '        HSD_GObjFree(gobj);\n',
         '    gp = alloc_user_data_ground();\n'
         '    if (gp != NULL && mp_slippi_rule(MP_SR_INIT_STAGE)) {\n'
         '        _Static_assert(sizeof(Ground) == 516, "Ground block size");\n'
         '        memset(gp, 0, 516);\n'
         '    }\n'
         '    if (gp == NULL) {\n'
         '        HSD_GObjFree(gobj);\n', 1),
    ],
    'melee/gr/grpstadium.c': [
        _decl('#include "grpstadium.h"\n'),
        # Common/PSCameraIndependentMonitor (801d24fc).
        ('            Player_8003219C(gp->u.display.xEE) || !grStadium_801D32D0(gobj))\n',
         '            Player_8003219C(gp->u.display.xEE) ||\n'
         '            !mp_slippi_ps_monitor_ok(gp->u.display.xEE,\n'
         '                                     grStadium_801D32D0(gobj)))\n', 1),
        # Online/Core/Hacks/Stadium/IngameCheckIfFrozen (801d457c): the
        # frozen toggle counts as the training-mode test.
        ('    temp_f31 = Ground_801C0498();\n'
         '    if (gm_8018841C() != 0) {\n',
         '    temp_f31 = Ground_801C0498();\n'
         '    if (gm_8018841C() != 0 || (mp_slippi_rule(MP_SR_PS_FROZEN) &&\n'
         '                               mp_slippi_rules_frozen_stadium())) {\n', 1),
        # Online/Core/Hacks/Stadium/StadiumFileLoad (800165ac): the
        # transformation loads synchronously, archive set up at once.
        ('            lbFile_80016580(datfiles[var_r29], gp->u.stadium.xCC,\n'
         '                            &gp->u.stadium.xC8, fn_801D4220, NULL);\n',
         '            if (mp_slippi_rule(MP_SR_PS_LOAD)) {\n'
         '                gp->u.stadium.xC8 = lbFileGetSize(datfiles[var_r29]);\n'
         '                lbFile_8001668C(datfiles[var_r29], gp->u.stadium.xCC,\n'
         '                                &gp->u.stadium.xC8);\n'
         '                gp->u.stadium.xD0 = grDatFiles_801C6478(\n'
         '                    gp->u.stadium.xCC, gp->u.stadium.xC8);\n'
         '            } else {\n'
         '                lbFile_80016580(datfiles[var_r29], gp->u.stadium.xCC,\n'
         '                                &gp->u.stadium.xC8, fn_801D4220, NULL);\n'
         '            }\n', 1),
        # Online/Core/Hacks/Stadium/GrPsxIsValid (801d4760).
        ('    case 1:\n'
         '        if (grStadium_801D42B8()) {\n',
         '    case 1:\n'
         '        if (mp_slippi_rule(MP_SR_PS_LOAD) ? mp_slippi_ps_archive_valid()\n'
         '                                          : grStadium_801D42B8()) {\n', 1),
        # The port's presentation overlay swaps the screen's live views (modes
        # 7 and 8) for the text display, which changes the monitor's random
        # timers. Slippi plays the vanilla modes; their images stay black.
        ('    if (arg1 == 7 || arg1 == 8) {\n'
         '        arg1 = gm_8018841C() ? 0 : 1;\n'
         '    }\n',
         '    if (!mp_slippi_rule(MP_SR_PS_LIVE_VIEW) && (arg1 == 7 || arg1 == 8)) {\n'
         '        arg1 = gm_8018841C() ? 0 : 1;\n'
         '    }\n', 1),
        ('    lb_800121FC(&wrapper->desc, 0x280, 0x196, 4, 0x7D3);\n',
         '    lb_800121FC(&wrapper->desc, 0x280, 0x196, 4, 0x7D3);\n'
         '    mp_slippi_clear_image(&wrapper->desc);\n', 1),
        ('    lb_800121FC(&wrapper->desc, 0x7C, 0x50, 4, 0x7D4);\n',
         '    lb_800121FC(&wrapper->desc, 0x7C, 0x50, 4, 0x7D4);\n'
         '    mp_slippi_clear_image(&wrapper->desc);\n', 1),
    ],
}
