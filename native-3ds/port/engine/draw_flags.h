#ifndef MP_DRAW_FLAGS_H
#define MP_DRAW_FLAGS_H

/* High bits of the existing packed blend word; the low 16 bits retain GX
 * blend/logic operations. Keep the 34-word BE8 draw descriptor unchanged. */
#define MP_DRAW_RGBA6_Z24 0x10000u
#define MP_DRAW_LEFT_CAPTURE 0x20000u

/* MPGPUUniforms.texgen_mode: nonzero enables renderer texgen. */
#define MP_TEXGEN_ENABLED 32u
#define MP_TEXGEN_SOURCE(mode) ((mode)&3u) /* 0 texture coordinate, 1 normal, 2 position */
#define MP_TEXGEN_3X4 4u
#define MP_TEXGEN_NORMALIZE 8u
#define MP_TEXGEN_POST 16u

#endif
