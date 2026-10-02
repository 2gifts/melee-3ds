/* Boot-time record of the engine's floating-point environment (Slippi
 * determinism experiment, docs/slippi/determinism.md). */
#include <mp_fpscr.h>
#include "native.h"

extern void OSReport(char*, ...);
unsigned mp_fp_exact_self_test(void);
unsigned mp_ppc_math_self_test(void);
unsigned mp_fp_boot_fpscr;

void mp_fp_boot_check(void)
{
    unsigned fpscr, vectors, estimates;
    __asm__ volatile("vmrs %0,fpscr" : "=r"(fpscr));
    mp_fp_boot_fpscr = fpscr;
    vectors = mp_fp_exact_self_test();  /* panics on a mismatch */
    estimates = mp_ppc_math_self_test(); /* frsqrte table, panics too */
    OSReport("FP: engine FPSCR=0x%08x (expected control 0x%08x, %s); "
             "GameCube contraction; MSL maths; fma/sqrtf vectors ok=%u, frsqrte ok=%u\n",
             fpscr, (unsigned) MP_ENGINE_FPSCR,
             (fpscr & MP_FPSCR_CONTROL_MASK) == (MP_ENGINE_FPSCR & MP_FPSCR_CONTROL_MASK)
                 ? (MP_ENGINE_FPSCR == MP_FPSCR_IEEE_RN ? "IEEE, round to nearest"
                                                        : "flush-to-zero, default NaN, round to nearest")
                 : "MISMATCH",
             vectors, estimates);
}
