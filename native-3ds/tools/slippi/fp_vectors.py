"""Reference vectors for the engine's GameCube-exact maths (port/engine/fp_exact.c).

Independent Python model, from first principles:
* fma(x, y, z): exact rational x*y+z (fractions.Fraction), rounded once to
  the nearest double (ties to even), including subnormals and overflow.
* MSL sqrtf / sqrtf_accurate: the Gekko frsqrte estimate (Dolphin's table,
  re-implemented here), then per step t = g*g, h = 0.5*g (double products),
  u = -(x*t - 3) rounded once (fnmsub), g = h*u, finally (float)(x*g).

    python tools/slippi/fp_vectors.py            # print a summary
The engine build writes build/compat/fp_exact_vectors.h from write_header().
"""
from fractions import Fraction
import math
import random
import struct


def d2u(d):
    return struct.unpack('>Q', struct.pack('>d', d))[0]


def u2d(u):
    return struct.unpack('>d', struct.pack('>Q', u))[0]


def f2u(f):
    return struct.unpack('>I', struct.pack('>f', f))[0]


def u2f(u):
    return struct.unpack('>f', struct.pack('>I', u))[0]


def round_double(q):
    """Fraction -> nearest double (ties to even); +0.0 for an exact zero."""
    if q == 0:
        return 0.0
    sign = -1.0 if q < 0 else 1.0
    q = abs(q)
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    # q in [2^e, 2^(e+1)); 53-bit significand unless subnormal
    e = max(e, -1022)
    scale = Fraction(2) ** (e - 52)
    m = q / scale
    n = m.numerator // m.denominator
    rem = m - n
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and n & 1):
        n += 1
    try:
        return sign * math.ldexp(n, e - 52)  # n == 2^53 after a carry is exact too
    except OverflowError:
        return sign * math.inf


def round_single(d):
    return struct.unpack('>f', struct.pack('>f', d))[0]


def fma(x, y, z):
    if not all(math.isfinite(v) for v in (x, y, z)):
        return x * y + z
    exact = Fraction(x) * Fraction(y) + Fraction(z)
    if exact == 0:
        return x * y + z  # sign of an exact zero as IEEE addition gives it
    return round_double(exact)


FRSQRTE = [(0x1a7e800, -0x568), (0x17cb800, -0x4f3), (0x1552800, -0x48d), (0x130c000, -0x435),
           (0x10f2000, -0x3e7), (0x0eff000, -0x3a2), (0x0d2e000, -0x365), (0x0b7c000, -0x32e),
           (0x09e5000, -0x2fc), (0x0867000, -0x2d0), (0x06ff000, -0x2a8), (0x05ab800, -0x283),
           (0x046a000, -0x261), (0x0339800, -0x243), (0x0218800, -0x226), (0x0105800, -0x20b),
           (0x3ffa000, -0x7a4), (0x3c29000, -0x700), (0x38aa000, -0x670), (0x3572000, -0x5f2),
           (0x3279000, -0x584), (0x2fb7000, -0x524), (0x2d26000, -0x4cc), (0x2ac0000, -0x47e),
           (0x2881000, -0x43a), (0x2665000, -0x3fa), (0x2468000, -0x3c2), (0x2287000, -0x38e),
           (0x20c1000, -0x35e), (0x1f12000, -0x332), (0x1d79000, -0x30a), (0x1bf4000, -0x2e6)]


def frsqrte(x):
    """Positive normal or subnormal double -> Gekko estimate (Dolphin's model)."""
    bits = d2u(x)
    mantissa = bits & ((1 << 52) - 1)
    exponent = bits & (0x7ff << 52)
    assert 0 < x < math.inf
    if not exponent:
        while not mantissa & (1 << 52):
            exponent -= 1 << 52
            mantissa <<= 1
        mantissa &= (1 << 52) - 1
        exponent += 1 << 52
    lsb = exponent & (1 << 52)
    exponent = ((0x3ff << 52) - ((exponent - (0x3fe << 52)) // 2)) & (0x7ff << 52)
    i = (lsb | mantissa) >> 37
    base, dec = FRSQRTE[i // 2048]
    return u2d(exponent | ((base + dec * (i % 2048)) << 26))


def msl_sqrtf(x, steps=3):
    if not x > 0:
        return x
    g = frsqrte(x)
    for _ in range(steps):
        t = g * g
        h = 0.5 * g
        g = h * -fma(x, t, -3.0)
    return round_single(x * g)


def fma_cases():
    rnd = random.Random(0x5eed)
    cases = [(1.0, 1.0, 1.0), (0.1, 10.0, -1.0), (u2d(0x3ff0000000000001), u2d(0x3ff0000000000001), -1.0),
             (1.5, 1.5, -2.25), (-1.5, 1.5, 2.25), (1e308, 10.0, -1e308), (u2d(1), 0.5, 0.0),
             (u2d(1), 0.5, u2d(1)), (2.0 ** -1000, 2.0 ** -60, 2.0 ** -1070),
             (3.0, 1.0 / 3.0, -1.0), (0.0, 5.0, -0.0), (-0.0, 5.0, 0.0), (1e-300, 1e-300, 1e-310)]
    for _ in range(60):
        x = rnd.uniform(-4, 4) * 2.0 ** rnd.randint(-30, 30)
        y = rnd.uniform(-4, 4) * 2.0 ** rnd.randint(-30, 30)
        z = -x * y * (1 + rnd.choice([0, 2 ** -52, -2 ** -53, 2 ** -30]))
        cases.append((x, y, z))
    for _ in range(30):  # sqrtf-style Newton steps
        x = round_single(rnd.uniform(0.001, 1e6))
        g = frsqrte(x)
        cases.append((x, g * g, -3.0))
    return [(x, y, z, fma(x, y, z)) for x, y, z in cases]


def sqrtf_cases():
    rnd = random.Random(0x5a7)
    xs = [1.0, 2.0, 4.0, 0.25, 9.0, 1e-20, 3.4e38, u2f(1), u2f(0x007fffff), 1234.5678, 0.5, 100.0,
          -1.0, 0.0, -0.0]
    xs += [u2f(rnd.randrange(0x00800000, 0x7f000000)) for _ in range(60)]
    xs = [round_single(x) for x in xs]
    return [(x, msl_sqrtf(x, 3), msl_sqrtf(x, 4)) for x in xs]


def write_header(path):
    lines = ['/* Generated by tools/slippi/fp_vectors.py */',
             'static const uint64_t mp_fma_vectors[][4]={']
    lines += ['{0x%016xULL,0x%016xULL,0x%016xULL,0x%016xULL},' % tuple(d2u(v) for v in c) for c in fma_cases()]
    lines += ['};', 'static const uint32_t mp_sqrtf_vectors[][3]={']
    lines += ['{0x%08x,0x%08x,0x%08x},' % tuple(f2u(v) for v in c) for c in sqrtf_cases()]
    lines += ['};']
    text = '\n'.join(lines) + '\n'
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


if __name__ == '__main__':
    exact = sum(1 for x, s, a in sqrtf_cases() if x > 0 and s == round_single(math.sqrt(x)))
    total = sum(1 for x, s, a in sqrtf_cases() if x > 0)
    print(f'{len(fma_cases())} fma vectors; MSL sqrtf equals the correctly rounded sqrt for {exact}/{total} inputs')
    for x, s, a in sqrtf_cases()[:12]:
        print(f'  sqrtf({x!r}) = {s!r} (0x{f2u(s):08x}); accurate {a!r}; correctly rounded {round_single(math.sqrt(x)) if x > 0 else x!r}')
