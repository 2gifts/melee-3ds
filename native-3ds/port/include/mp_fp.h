/* GameCube floating-point rounding helpers, force-included ahead of every
 * decomp source the engine build compiles with contraction
 * (tools/engine_build.py; docs/slippi/determinism.md).
 *
 * Names follow Melee Unlocked's source port (GPL-3.0-or-later,
 * sourceport/game/include/mu_native.h and math_native.h), whose source
 * annotations tools/slippi_edits/determinism.py carries over. */
#ifndef MP_FP_H
#define MP_FP_H

/* One product rounded on its own before the surrounding sum: clang contracts
 * only within one expression, and a statement expression ends it (verified:
 * the product is stored to a local before the add sees it). */
#define MU_P(x) ({ __typeof__(x) mu_p_ = (x); mu_p_; })

/* Explicit Gekko single-precision fused operations, as Dolphin rounds them:
 * the float product is exact in double, one rounding in double, then one to
 * single. Contraction is off inside, so nothing is fused twice. */
static inline __attribute__((always_inline)) float mp_fmadds(float a, float c, float b)
{
#pragma clang fp contract(off)
    return (float) ((double) a * (double) c + (double) b);
}
static inline __attribute__((always_inline)) float mp_fmsubs(float a, float c, float b)
{
#pragma clang fp contract(off)
    return (float) ((double) a * (double) c - (double) b);
}
/* fnmsubs: -(a*c - b). */
static inline __attribute__((always_inline)) float mp_fnmsubs(float a, float c, float b)
{
#pragma clang fp contract(off)
    return -(float) ((double) a * (double) c - (double) b);
}
#define mu_fmadds mp_fmadds
#define mu_fmsubs mp_fmsubs
#define mu_fnmsubs mp_fnmsubs
#define __fnmsubs mp_fnmsubs
#define MU_FMADDS(a, b, c) mp_fmadds((float) (a), (float) (b), (float) (c))

/* Functions where the console compiler fused nothing are listed in
 * tools/slippi/no_contract.txt (applied to the IR); the tag is a marker. */
#define MU_NO_CONTRACT

float mp_sqrtf_accurate(float x); /* MSL sqrtf with a fourth Newton step */
double mp_fres(double x);         /* Gekko fres estimate */

#endif
