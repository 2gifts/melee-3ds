/* GameCube-exact floating-point support for the engine (Slippi experiment).
 * See docs/slippi/determinism.md.
 *
 * Compiled without contraction (port/engine sources are -ffp-contract=off):
 * every fused operation below is spelled out.
 *
 * mp_fma: correctly rounded double fused multiply-add. Adapted from musl
 * src/math/fma.c (MIT, Copyright (c) 2005-2020 Rich Felker et al.), with
 * the normalization and the final rounding done in integers, so the result
 * does not depend on the VFP's flush-to-zero mode or on libgcc.
 *
 * mp_fres: Gekko reciprocal estimate, adapted from Dolphin
 * Common/FloatUtils.cpp ApproximateReciprocal (GPL-2.0-or-later, Dolphin
 * Emulator Project), via Melee Unlocked sourceport/game/math/estimates.c
 * (GPL-3.0-or-later).
 */
#include <stdint.h>
#include "native.h"

double mp_frsqrte(double);

typedef union { double d; uint64_t u; } Bits64;
typedef union { float f; uint32_t u; } Bits32;

struct mp_num { uint64_t m; int e; int sign; };

#define ZEROINFNAN (0x7ff - 0x3ff - 52 - 1)

static struct mp_num normalize(uint64_t ix)
{
    int e = (int) (ix >> 52);
    int sign = e & 0x800;
    e &= 0x7ff;
    uint64_t m = ix & ((1ULL << 52) - 1);
    if (!e) {
        if (!m) {
            /* zero: flagged like musl (e >= ZEROINFNAN) */
            return (struct mp_num) { 0, 0x800 - 0x3ff - 52 - 1, sign };
        }
        /* subnormal: shift the leading one to bit 52 */
        int shift = __builtin_clzll(m) - 11;
        m <<= shift;
        e = 1 - shift;
    }
    m &= (1ULL << 52) - 1;
    m |= 1ULL << 52;
    m <<= 1;
    e -= 0x3ff + 52 + 1;
    return (struct mp_num) { m, e, sign };
}

static void mul64(uint64_t* hi, uint64_t* lo, uint64_t x, uint64_t y)
{
    uint64_t t1, t2, t3;
    uint64_t xlo = (uint32_t) x, xhi = x >> 32;
    uint64_t ylo = (uint32_t) y, yhi = y >> 32;
    t1 = xlo * ylo;
    t2 = xlo * yhi + xhi * ylo;
    t3 = xhi * yhi;
    *lo = t1 + (t2 << 32);
    *hi = t3 + (t2 >> 32) + (t1 > *lo);
}

/* sign * r * 2^e, r in [2^62, 2^63) with bit 0 sticky: round to nearest
 * even into a double, including subnormal results and overflow. */
static double pack(uint64_t r, int e, int sign)
{
    int biased = e + 62 + 1023;
    uint64_t s = sign ? 1ULL << 63 : 0, mant, rem, half;
    int shift = 10;
    if (biased < 1) shift += 1 - biased;
    if (shift >= 64) {
        mant = 0;
        rem = r;
        half = shift == 64 ? 1ULL << 63 : ~0ULL;
        if (shift > 64) rem = 1; /* strictly below half */
    } else {
        mant = r >> shift;
        rem = r & ((1ULL << shift) - 1);
        half = 1ULL << (shift - 1);
    }
    if (rem > half || (rem == half && (mant & 1))) mant++;
    Bits64 out;
    if (biased < 1) {
        /* subnormal (a carry into bit 52 makes it the smallest normal) */
        out.u = s | mant;
        return out.d;
    }
    if (mant >> 53) {
        mant >>= 1;
        biased++;
    }
    if (biased >= 0x7ff) {
        out.u = s | (0x7ffULL << 52);
        return out.d;
    }
    out.u = s | ((uint64_t) biased << 52) | (mant & ((1ULL << 52) - 1));
    return out.d;
}

double mp_fma(double x, double y, double z)
{
    Bits64 bx = { x }, by = { y }, bz = { z };
    struct mp_num nx = normalize(bx.u), ny = normalize(by.u), nz = normalize(bz.u);

    if (nx.e >= ZEROINFNAN || ny.e >= ZEROINFNAN) return x * y + z;
    if (nz.e >= ZEROINFNAN) {
        if (nz.e > ZEROINFNAN) return x * y; /* z == 0 */
        return z;
    }

    uint64_t rhi, rlo, zhi, zlo;
    mul64(&rhi, &rlo, nx.m, ny.m);

    int e = nx.e + ny.e;
    int d = nz.e - e;
    if (d > 0) {
        if (d < 64) {
            zlo = nz.m << d;
            zhi = nz.m >> (64 - d);
        } else {
            zlo = 0;
            zhi = nz.m;
            e = nz.e - 64;
            d -= 64;
            if (d == 0) {
            } else if (d < 64) {
                rlo = rhi << (64 - d) | rlo >> d | !!(rlo << (64 - d));
                rhi = rhi >> d;
            } else {
                rlo = 1;
                rhi = 0;
            }
        }
    } else {
        zhi = 0;
        d = -d;
        if (d == 0) {
            zlo = nz.m;
        } else if (d < 64) {
            zlo = nz.m >> d | !!(nz.m << (64 - d));
        } else {
            zlo = 1;
        }
    }

    int sign = nx.sign ^ ny.sign;
    int samesign = !(sign ^ nz.sign);
    int nonzero = 1;
    if (samesign) {
        rlo += zlo;
        rhi += zhi + (rlo < zlo);
    } else {
        uint64_t t = rlo;
        rlo -= zlo;
        rhi = rhi - zhi - (t < rlo);
        if (rhi >> 63) {
            rlo = -rlo;
            rhi = -rhi - !!rlo;
            sign = !sign;
        }
        nonzero = !!rhi;
    }

    if (nonzero) {
        e += 64;
        d = __builtin_clzll(rhi) - 1;
        rhi = rhi << d | rlo >> (64 - d) | !!(rlo << d);
    } else if (rlo) {
        d = __builtin_clzll(rlo) - 1;
        if (d < 0)
            rhi = rlo >> 1 | (rlo & 1);
        else
            rhi = rlo << d;
    } else {
        /* exact +-0: x*y == -z exactly, so this rounds nothing */
        return x * y + z;
    }
    e -= d;
    return pack(rhi, e, sign);
}

/* PowerPC fnmsub (double): -(a*c - b), one rounding. */
static double fnmsub(double a, double c, double b)
{
    return -mp_fma(a, c, -b);
}

/* MSL math_ppc.h sqrtf, as the console executes the inline function
 * (checked against lbVector_Normalize, 0x8000D31C):
 *   frsqrte g; then per step: t = g*g; h = 0.5*g; u = fnmsub(x, t, 3.0);
 *   g = h*u; finally (float)(x*g). */
static float msl_sqrtf(float x, int steps)
{
    if (x > 0.0f) {
        double xd = x;
        double guess = mp_frsqrte(xd);
        for (int i = 0; i < steps; ++i) {
            double t = guess * guess;
            double h = 0.5 * guess;
            guess = h * fnmsub(xd, t, 3.0);
        }
        return (float) (xd * guess);
    }
    return x;
}

float mp_be_sqrtf(float x) { return msl_sqrtf(x, 3); }
float mp_sqrtf_accurate(float x) { return msl_sqrtf(x, 4); }

/* MSL math.h fmodf (retail copy at 0x80364340):
 *   |b| > |a| ? a : fnmsubs(b, (float)(long long)(a/b), a) */
float mp_be_fmodf(float a, float b)
{
    float fa = __builtin_fabsf(a), fb = __builtin_fabsf(b);
    if (fb > fa) return a;
    long long quotient = (long long) (a / b);
    float q = (float) quotient;
    return -(float) ((double) b * (double) q - (double) a);
}

double mp_fres(double val)
{
    static const int base[] = {
        0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200, 0x579000, 0x520800,
        0x4cc800, 0x47ca00, 0x430800, 0x3e8000, 0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
        0x2aa800, 0x272c00, 0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
        0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000, 0x041c00, 0x020c00,
    };
    static const int dec[] = {
        0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0, 0x27f, 0x261, 0x245, 0x22a,
        0x212, 0x1fb, 0x1e5, 0x1d1, 0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e, 0x15b, 0x15b,
        0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106,
    };
    Bits64 v = { val };
    uint64_t mantissa = v.u & ((1ULL << 52) - 1);
    uint64_t sign = v.u & (1ULL << 63);
    int64_t exponent = (int64_t) (v.u & (0x7ffULL << 52));
    if (!mantissa && !exponent) {
        v.u = sign | (0x7ffULL << 52); /* +-inf */
        return v.d;
    }
    if (exponent == (int64_t) (0x7ffULL << 52)) {
        if (!mantissa) {
            v.u = sign; /* +-0 */
            return v.d;
        }
        v.u |= 1ULL << 51; /* quiet NaN, payload kept */
        return v.d;
    }
    if (exponent < (895LL << 52)) {
        Bits32 big = { .u = 0x7f7fffff };
        Bits64 r = { (double) big.f };
        r.u |= sign;
        return r.d;
    }
    if (exponent >= (1149LL << 52)) {
        v.u = sign;
        return v.d;
    }
    exponent = (0x7fdLL << 52) - exponent;
    int i = (int) (mantissa >> 37);
    v.u = sign | (uint64_t) exponent;
    v.u |= (uint64_t) (int64_t) (base[i / 1024] - (dec[i / 1024] * (i % 1024) + 1) / 2) << 29;
    return v.d;
}

/* ---- development self-test (smoke builds call it through the bridge) ---- */

/* {x, y, z, fma(x,y,z)} as bit patterns; the results were derived by exact
 * rational arithmetic (tools/slippi/fp_vectors.py). */
#include <fp_exact_vectors.h>

static int subnormal64(uint64_t u) { return !(u & (0x7ffULL << 52)) && (u << 1); }
static int subnormal32(uint32_t u) { return !(u & 0x7f800000u) && (u << 1); }

/* Returns the number of vectors checked. In flush-to-zero mode (the
 * hardware-safe default, mp_fpscr.h) vectors with a subnormal input or
 * result are skipped: the VFP flushes those, the console does not. */
unsigned mp_fp_exact_self_test(void)
{
    unsigned i, n = 0, fpscr;
    __asm__ volatile("vmrs %0,fpscr" : "=r"(fpscr));
    int flush = (fpscr >> 24) & 1;
    for (i = 0; i < sizeof(mp_fma_vectors) / sizeof(*mp_fma_vectors); ++i) {
        const uint64_t* v = mp_fma_vectors[i];
        if (flush && (subnormal64(v[0]) || subnormal64(v[1]) || subnormal64(v[2]) || subnormal64(v[3])))
            continue;
        Bits64 x = { .u = v[0] }, y = { .u = v[1] }, z = { .u = v[2] }, r;
        r.d = mp_fma(x.d, y.d, z.d);
        if (r.u != v[3]) mp_platform_panic("Software fma mismatch");
        ++n;
    }
    for (i = 0; i < sizeof(mp_sqrtf_vectors) / sizeof(*mp_sqrtf_vectors); ++i) {
        const uint32_t* v = mp_sqrtf_vectors[i];
        if (flush && subnormal32(v[0])) continue;
        Bits32 x = { .u = v[0] }, r;
        r.f = mp_be_sqrtf(x.f);
        if (r.u != v[1]) mp_platform_panic("MSL sqrtf mismatch");
        r.f = mp_sqrtf_accurate(x.f);
        if (r.u != v[2]) mp_platform_panic("MSL sqrtf_accurate mismatch");
        ++n;
    }
    return n;
}
