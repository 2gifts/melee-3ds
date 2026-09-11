#ifndef _DOLPHIN_TYPES_H_
#define _DOLPHIN_TYPES_H_
/* The decomp's 'long' types are not fixed-width on every host. Match the
 * 32-bit GameCube ABI while agreeing with libctru's typedefs on ARM. */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;
typedef char* Ptr;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
_Static_assert(sizeof(u32) == 4 && sizeof(f32) == 4, "GameCube scalar ABI");
#endif
