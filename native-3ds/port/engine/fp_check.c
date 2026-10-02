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
    {   /* Cost of the console maths, for hardware logs (ticks of the
         * engine timebase, 40.5 MHz, per 256 calls). */
        extern unsigned mp_platform_ticks(void);
        extern float mp_be_sqrtf(float), mp_be_sinf(float);
        extern double mp_fma(double, double, double);
        volatile float sink = 0, x = 1.2345f;
        volatile double dsink = 0, dx = 1.2345;
        unsigned t0 = mp_platform_ticks(), i;
        for (i = 0; i < 256; ++i) sink = mp_be_sqrtf(x + (float) i);
        unsigned t1 = mp_platform_ticks();
        for (i = 0; i < 256; ++i) sink = __builtin_sqrtf(x + (float) i);
        unsigned t2 = mp_platform_ticks();
        for (i = 0; i < 256; ++i) sink = mp_be_sinf(x + (float) i);
        unsigned t3 = mp_platform_ticks();
        for (i = 0; i < 256; ++i) dsink = mp_fma(dx, dx + i, -3.0);
        unsigned t4 = mp_platform_ticks();
        (void) sink; (void) dsink;
        OSReport("FP: cost per 256 calls (40.5 MHz ticks): MSL sqrtf %u, VFP sqrt %u, MSL sinf %u, software fma %u\n",
                 t1 - t0, t2 - t1, t3 - t2, t4 - t3);
    }
    OSReport("FP: engine FPSCR=0x%08x (expected control 0x%08x, %s); "
             "GameCube contraction; MSL maths; fma/sqrtf vectors ok=%u, frsqrte ok=%u\n",
             fpscr, (unsigned) MP_ENGINE_FPSCR,
             (fpscr & MP_FPSCR_CONTROL_MASK) == (MP_ENGINE_FPSCR & MP_FPSCR_CONTROL_MASK)
                 ? (MP_ENGINE_FPSCR == MP_FPSCR_IEEE_RN ? "IEEE, round to nearest"
                                                        : "flush-to-zero, default NaN, round to nearest")
                 : "MISMATCH",
             vectors, estimates);
}
