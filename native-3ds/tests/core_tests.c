#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "melee_port/archive.h"
#include "melee_port/input.h"
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/spline.h>

static void be32(uint8_t *p, uint32_t n)
{
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}

static void archives(void)
{
    /* Synthetic HSD: one data pointer (to offset zero), one float and root. */
    uint8_t b[65] = {0};
    be32(b, sizeof(b)); be32(b+4, 16); be32(b+8, 1); be32(b+12, 1);
    be32(b+36, 0x3fc00000); be32(b+48, 0); be32(b+52, 4);
    memcpy(b+60, "root", 5);
    MpArchive a;
    MpArchiveSymbol sym;
    uint32_t n = 99;
    float f;
    assert(mp_archive_open(&a, b, sizeof(b)) == 0);
    assert(mp_archive_public(&a, 0, &sym) == 0 && sym.offset == 4);
    assert(!strcmp(sym.name, "root"));
    assert(mp_archive_find(&a, "root", &n) == 0 && n == 4);
    assert(mp_archive_find(&a, "absent", &n) == -1);
    assert(mp_archive_pointer(&a, 0, &n) == 0 && n == 0);
    assert(mp_archive_pointer(&a, 4, &n) == -1);
    assert(mp_archive_f32(&a, 4, &f) == 0 && f == 1.5f);
    assert(mp_archive_u32(&a, UINT32_MAX, &n) == -1);
    assert(mp_archive_public(&a, 1, &sym) == -1);
    for (size_t i = 0; i < sizeof(b); ++i)
        assert(mp_archive_open(&a, b, i) == -1);
    b[64] = 'x'; assert(mp_archive_open(&a, b, sizeof(b)) == -1); b[64] = 0;
    be32(b+8, UINT32_MAX); assert(mp_archive_open(&a, b, sizeof(b)) == -1);
    be32(b+8, 1); be32(b+48, 15);
    assert(mp_archive_open(&a, b, sizeof(b)) == -1);
    be32(b+48, 0); be32(b+32, 16);
    assert(mp_archive_open(&a, b, sizeof(b)) == -1);
    be32(b+32, 0); be32(b+12, 0); be32(b+16, 1);
    be32(b+52, 0); /* External link forms a self-cycle at data offset zero. */
    assert(mp_archive_open(&a, b, sizeof(b)) == -1);
    be32(b+32, UINT32_MAX);
    be32(b+8, 0); /* Layout changes, construct a separate external fixture. */
    uint8_t ex[46] = {0};
    be32(ex, sizeof(ex)); be32(ex+4, 4); be32(ex+16, 1);
    be32(ex+32, UINT32_MAX); memcpy(ex+44, "e", 2);
    assert(mp_archive_open(&a, ex, sizeof(ex)) == 0);
}

static void inputs(void)
{
    MpInput in = {0}; PADStatus p[4];
    mp_input_map(&in, p);
    assert(p[0].err == 0 && p[0].button == 0);
    for (int i=1; i<4; ++i) assert(p[i].err == PAD_ERR_NO_CONTROLLER);
    in.held = (1u<<0) | (1u<<9) | (1u<<14) | (1u<<3);
    in.circle_x = 156; in.circle_y = -156;
    in.cstick_x = 10000; in.cstick_y = -10000;
    mp_input_map(&in, p);
    assert(p[0].button == (PAD_BUTTON_A | PAD_TRIGGER_L | PAD_TRIGGER_Z | PAD_BUTTON_START));
    assert(p[0].stickX == 80 && p[0].stickY == -80);
    assert(p[0].substickX == 80 && p[0].substickY == -80);
    assert(p[0].triggerLeft == 255 && p[0].triggerRight == 0);
    assert(p[0].analogA == 255 && p[0].analogB == 0);
}

static void upstream_engine(void)
{
    /* Known recurrence results, independently calculated from the GC LCG. */
    const int expected[] = {41, 51235, 6334, 59268, 51937};
    *seed_ptr = 1;
    for (size_t i=0; i<sizeof(expected)/sizeof(expected[0]); ++i)
        assert(HSD_Rand() == expected[i]);
    assert(splGetHelmite(1.f, 0.f, 2.f, 10.f, 0.f, 0.f) == 2.f);
    assert(splGetHelmite(1.f, 1.f, 2.f, 10.f, 0.f, 0.f) == 10.f);
    assert(fabsf(splGetHelmite(1.f, .5f, 2.f, 10.f, 0.f, 0.f) - 6.f) < 1e-6f);
    Vec3 cv[] = {{0,0,0}, {10,20,30}, {20,0,10}};
    HSD_Spline s = {.type=0, .numcv=3, .cv=cv}; Vec3 out;
    splGetSplinePoint(&out, &s, .25f);
    assert(out.x == 5.f && out.y == 10.f && out.z == 15.f);
    splGetSplinePoint(&out, &s, 1.f);
    assert(out.x == 20.f && out.y == 0.f && out.z == 10.f);
}

int main(void)
{
    archives(); inputs(); upstream_engine();
    puts("PASS: HSD endian/bounds/extern-chain tests, PAD mapping, upstream RNG/splines");
    return 0;
}
