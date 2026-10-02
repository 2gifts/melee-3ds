/* Force-included ahead of the MSL maths sources the engine compiles
 * (tools/engine_build.py, Slippi determinism). MSL's own math.h clashes with
 * newlib's, which the rest of the engine uses; this provides the few things
 * the MSL .c files take from it, and its guard keeps it out. */
#ifndef MP_MSL_MATH_H
#define MP_MSL_MATH_H
#define MSL_MATH_H
#include <Runtime/platform.h>
#include <math.h>
#define MSL_HI(x) *(int*) &x
#define MSL_LO(x) *(1 + (int*) &x)
float fabsf__Ff(float);
float sin__Ff(float x);
float cos__Ff(float x);
#endif
