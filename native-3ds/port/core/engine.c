#include "melee_port/engine.h"
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/spline.h>

int mp_engine_selftest(void)
{
    *seed_ptr = 1;
    return HSD_Rand() == 41 &&
        splGetHelmite(1.f, .5f, 2.f, 10.f, 0.f, 0.f) == 6.f;
}

void mp_engine_curve(float t, float *x, float *y)
{
    Vec3 points[] = {{24,190,0}, {120,38,0}, {270,212,0}, {376,50,0}};
    HSD_Spline spline = {.type=1, .numcv=2, .cv=points};
    Vec3 v = {0};
    splGetSplinePoint(&v, &spline, t);
    *x = v.x; *y = v.y;
}
