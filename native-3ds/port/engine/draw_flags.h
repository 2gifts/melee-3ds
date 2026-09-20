#ifndef MP_DRAW_FLAGS_H
#define MP_DRAW_FLAGS_H

/* High bits of the existing packed blend word; the low 16 bits retain GX
 * blend/logic operations. Keep the 34-word BE8 draw descriptor unchanged. */
#define MP_DRAW_RGBA6_Z24 0x10000u
#define MP_DRAW_LEFT_CAPTURE 0x20000u

#endif
