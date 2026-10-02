/* Slippi determinism (docs/slippi/determinism.md), after Melee Unlocked's
 * sysdolphin/baselib/jobj.h change (GPL-3.0-or-later): the console inlines
 * HSD_JObjAdd{Rotation,Scale,Translation}{X,Y,Z} and fuses `field += a * b`
 * into one fmadds/fnmsubs (ft_800CB6EC: rotate.y - DegToRad(180/n) is one
 * fnmsubs at 0x800CB76C). As functions, the product is rounded at the call;
 * as macros the add and the product form one expression and clang fuses them
 * the same way. Force-included (tools/engine_build.py) after jobj.h into the
 * decomp sources that call these. */
#ifndef MP_JOBJ_FUSED_H
#define MP_JOBJ_FUSED_H
#include <sysdolphin/baselib/jobj.h>
#define MP_JOBJ_ADD(jobj_, field_, v_, line_)                                   \
    do {                                                                        \
        HSD_JObj* mp_j_ = (jobj_);                                              \
        HSD_ASSERT(line_, mp_j_);                                               \
        mp_j_->field_ += (float) (v_);                                          \
        if (!(mp_j_->flags & JOBJ_MTX_INDEP_SRT)) {                             \
            HSD_JObjSetMtxDirty(mp_j_);                                         \
        }                                                                       \
    } while (0)
#define HSD_JObjAddRotationX(j, v) MP_JOBJ_ADD(j, rotate.x, v, 1029)
#define HSD_JObjAddRotationY(j, v) MP_JOBJ_ADD(j, rotate.y, v, 1041)
#define HSD_JObjAddRotationZ(j, v) MP_JOBJ_ADD(j, rotate.z, v, 1053)
#define HSD_JObjAddScaleX(j, v) MP_JOBJ_ADD(j, scale.x, v, 1065)
#define HSD_JObjAddScaleY(j, v) MP_JOBJ_ADD(j, scale.y, v, 1077)
#define HSD_JObjAddScaleZ(j, v) MP_JOBJ_ADD(j, scale.z, v, 1089)
#define HSD_JObjAddTranslationX(j, v) MP_JOBJ_ADD(j, translate.x, v, 1102)
#define HSD_JObjAddTranslationY(j, v) MP_JOBJ_ADD(j, translate.y, v, 1114)
#define HSD_JObjAddTranslationZ(j, v) MP_JOBJ_ADD(j, translate.z, v, 1126)
#endif
