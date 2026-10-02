/* Paired-single SDK matrix/vector routines, rounded like the console.
 *
 * Adapted from Melee Unlocked sourceport/game/math/sdk_math.c
 * (GPL-3.0-or-later), itself based on the decomp's
 * extern/dolphin/src/dolphin/mtx/{mtx,mtxvec,vec}.c paired-single assembly.
 * Melee Unlocked checked these against the retail code.
 *
 * Each helper is one Gekko instruction as Dolphin computes it: every
 * operand here is a float, so a product is exact in double and
 *   mul/add/sub = one IEEE single operation (double rounding of a
 *                 float-operand +,-,* through double is innocuous),
 *   mad/msub/nmsub = ps_madd/ps_msub/ps_nmsub = round_single(round_double(
 *                 a*c +- b)), written with ordinary double operations.
 * Compiled with -ffp-contract=off (port/engine): nothing else fuses.
 * Only the routines the retail game contains are here; the remaining PS
 * names stay aliases of the SDK's C versions (tools/engine_math.py).
 */
#include <dolphin/mtx.h>
#include <stdint.h>

double mp_frsqrte(double);
double mp_fres(double);
float sinf(float), cosf(float);

/* For float operands a single-precision IEEE operation equals Dolphin's
 * double-then-single rounding (53 >= 2*24+2), so these are plain VFP ops. */
static inline float mul(float a, float b) { return a * b; }
static inline float add(float a, float b) { return a + b; }
static inline float sub(float a, float b) { return a - b; }
static inline float mad(float a, float b, float c)
{ return (float) ((double) a * (double) b + (double) c); }
static inline float msub(float a, float b, float c)
{ return (float) ((double) a * (double) b - (double) c); }
/* ps_nmsub: -(a*b - c) */
static inline float nmsub(float a, float b, float c)
{ return -(float) ((double) a * (double) b - (double) c); }

/* The reciprocal-square-root estimate carries 26 significant bits. Dolphin
 * rounds the second (frC) operand of a single-precision multiply to 25 bits
 * (Force25Bit), so est*est is est * round25(est), exact in double. */
static double significand25(double x)
{
    union { double d; uint64_t u; } v = { x };
    v.u = (v.u & 0xfffffffff8000000ULL) + (v.u & 0x8000000ULL);
    return v.d;
}
static float inverse_length(float squared)
{
    double estimate = mp_frsqrte(squared);
    float square = (float) (estimate * significand25(estimate));
    float half = (float) (estimate * 0.5);
    return mul(nmsub(square, squared, 3.0f), half);
}
static float length_squared(Vec v) { return add(mad(v.z, v.z, mul(v.x, v.x)), mul(v.y, v.y)); }

void PSMTXIdentity(Mtx m)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) m[r][c] = r == c ? 1.0f : 0.0f;
}
void PSMTXCopy(Mtx src, Mtx dst)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) dst[r][c] = src[r][c];
}
void PSMTXConcat(Mtx a, Mtx b, Mtx dst)
{
    Mtx out;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) {
            float v = mul(b[0][c], a[r][0]);
            v = mad(b[1][c], a[r][1], v);
            v = mad(b[2][c], a[r][2], v);
            if (c >= 2) v = mad(c == 3 ? 1.0f : 0.0f, a[r][3], v);
            out[r][c] = v;
        }
    PSMTXCopy(out, dst);
}
void PSMTXTranspose(Mtx src, Mtx dst)
{
    Mtx out;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) out[r][c] = src[c][r];
        out[r][3] = 0.0f;
    }
    PSMTXCopy(out, dst);
}
u32 PSMTXInverse(Mtx a, Mtx dst)
{
    Mtx v;
    v[0][0] = msub(a[1][1], a[2][2], mul(a[2][1], a[1][2]));
    v[1][0] = msub(a[1][2], a[2][0], mul(a[2][2], a[1][0]));
    v[0][1] = msub(a[2][1], a[0][2], mul(a[0][1], a[2][2]));
    v[1][1] = msub(a[2][2], a[0][0], mul(a[0][2], a[2][0]));
    v[0][2] = msub(a[0][1], a[1][2], mul(a[1][1], a[0][2]));
    v[1][2] = msub(a[0][2], a[1][0], mul(a[1][2], a[0][0]));
    v[2][0] = msub(a[1][0], a[2][1], mul(a[1][1], a[2][0]));
    v[2][1] = msub(a[0][1], a[2][0], mul(a[0][0], a[2][1]));
    v[2][2] = msub(a[0][0], a[1][1], mul(a[0][1], a[1][0]));
    float det = mad(a[2][0], v[0][2], mad(a[1][0], v[0][1], mul(a[0][0], v[0][0])));
    if (det == 0.0f) return 0;
    float estimate = (float) mp_fres(det);
    float reciprocal = nmsub(det, mul(estimate, estimate), add(estimate, estimate));
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) v[r][c] = mul(v[r][c], reciprocal);
        /* ps_nmadd: -(v2*a23 + (v1*a13 + v0*a03)) */
        v[r][3] = -(float) ((double) v[r][2] * (double) a[2][3] +
                            (double) mad(v[r][1], a[1][3], mul(v[r][0], a[0][3])));
    }
    PSMTXCopy(v, dst);
    return 1;
}
void PSMTXRotTrig(Mtx m, char axis, f32 s, f32 c)
{
    switch (axis | 0x20) {
    case 'x': PSMTXIdentity(m); m[1][1] = c; m[1][2] = -s; m[2][1] = s; m[2][2] = c; break;
    case 'y': PSMTXIdentity(m); m[0][0] = c; m[0][2] = s; m[2][0] = -s; m[2][2] = c; break;
    case 'z': PSMTXIdentity(m); m[0][0] = c; m[0][1] = -s; m[1][0] = s; m[1][1] = c; break;
    }
}
void PSMTXTrans(Mtx m, f32 x, f32 y, f32 z)
{
    PSMTXIdentity(m); m[0][3] = x; m[1][3] = y; m[2][3] = z;
}
void PSMTXScale(Mtx m, f32 x, f32 y, f32 z)
{
    PSMTXIdentity(m); m[0][0] = x; m[1][1] = y; m[2][2] = z;
}
void PSMTXMultVec(Mtx44 m, Vec* src, Vec* dst)
{
    Vec in = *src, out;
    float v[3];
    for (int r = 0; r < 3; r++)
        v[r] = add(mad(m[r][2], in.z, mul(m[r][0], in.x)), mad(m[r][3], 1.0f, mul(m[r][1], in.y)));
    out.x = v[0]; out.y = v[1]; out.z = v[2]; *dst = out;
}
void PSMTXMultVecSR(Mtx44 m, Vec* src, Vec* dst)
{
    Vec in = *src, out;
    float v[3];
    for (int r = 0; r < 3; r++) v[r] = mad(m[r][2], in.z, add(mul(m[r][0], in.x), mul(m[r][1], in.y)));
    out.x = v[0]; out.y = v[1]; out.z = v[2]; *dst = out;
}
void PSVECAdd(Vec* a, Vec* b, Vec* dst)
{ Vec v = { add(a->x, b->x), add(a->y, b->y), add(a->z, b->z) }; *dst = v; }
void PSVECSubtract(Vec* a, Vec* b, Vec* dst)
{ Vec v = { sub(a->x, b->x), sub(a->y, b->y), sub(a->z, b->z) }; *dst = v; }
void PSVECScale(Vec* a, Vec* dst, f32 scale)
{ Vec v = { mul(a->x, scale), mul(a->y, scale), mul(a->z, scale) }; *dst = v; }
void PSVECNormalize(Vec* src, Vec* dst)
{
    Vec v = *src;
    float inv = inverse_length(length_squared(v));
    PSVECScale(&v, dst, inv);
}
f32 PSVECMag(Vec* src)
{
    float squared = length_squared(*src), inv = inverse_length(squared);
    return mul(squared, inv >= 0.0f ? inv : squared); /* ps_sel */
}
f32 PSVECDotProduct(Vec* a, Vec* b) { return add(mad(a->x, b->x, mul(a->y, b->y)), mul(a->z, b->z)); }
void PSVECCrossProduct(Vec* a, Vec* b, Vec* dst)
{
    Vec v = { msub(a->y, b->z, mul(b->y, a->z)),
              -msub(a->x, b->z, mul(b->x, a->z)),
              -msub(a->y, b->x, mul(b->y, a->x)) };
    *dst = v;
}
void PSMTXRotAxisRad(Mtx m, Vec* axis, f32 angle)
{
    float s = sinf(angle), c = cosf(angle), t = sub(1.0f, c);
    Vec n;
    PSVECNormalize(axis, &n);
    float tx = mul(n.x, t), ty = mul(n.y, t), tz = mul(n.z, t);
    float xy = mul(tx, n.y), xz = mul(tx, n.z), yz = mul(ty, n.z);
    float sx = mul(n.x, s), sy = mul(n.y, s);
    m[0][0] = add(mul(tx, n.x), c); m[0][1] = nmsub(n.z, s, xy); m[0][2] = add(xz, sy); m[0][3] = 0;
    m[1][0] = mad(n.z, s, xy); m[1][1] = add(mul(ty, n.y), c); m[1][2] = add(-sx, yz); m[1][3] = 0;
    m[2][0] = sub(xz, sy); m[2][1] = add(sx, yz); m[2][2] = add(mul(tz, n.z), c); m[2][3] = 0;
}
void PSMTXQuat(Mtx m, Quaternion* q)
{
    float x = q->x, y = q->y, z = q->z, w = q->w;
    float xx = mul(x, x), yy = mul(y, y), zz = mul(z, z), ww = mul(w, w);
    (void) ww;
    float sum = add(mad(z, z, xx), mad(w, w, yy));
    float estimate = (float) mp_fres(sum);
    float scale = mul(mul(estimate, nmsub(sum, estimate, 2.0f)), 2.0f);
    float wz = mul(z, w), wy = mul(y, w), wx = mul(x, w);
    float xzwy = mad(x, z, wy), yzwx = mad(y, z, wx);
    m[0][0] = nmsub(add(yy, zz), scale, 1.0f); m[0][1] = mul(msub(x, y, wz), scale); m[0][2] = mul(xzwy, scale); m[0][3] = 0;
    m[1][0] = mul(mad(x, y, wz), scale); m[1][1] = nmsub(mad(z, z, xx), scale, 1.0f); m[1][2] = mul(nmsub(wx, 2.0f, yzwx), scale); m[1][3] = 0;
    m[2][0] = mul(nmsub(wy, 2.0f, xzwy), scale); m[2][1] = mul(yzwx, scale); m[2][2] = nmsub(add(xx, yy), scale, 1.0f); m[2][3] = 0;
}
