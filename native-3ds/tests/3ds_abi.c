#include <citro2d.h>
#include <stddef.h>
/* devkitARM libraries use packed enums. Engine enums remain 32-bit in other
 * translation units; never pass SDK structs across that interface. */
_Static_assert(sizeof(bool) == 1, "libctru boolean ABI");
_Static_assert(sizeof(GPU_COLORBUF) == 1, "devkitARM enum ABI");
_Static_assert(sizeof(C3D_FrameBuf) == 16, "citro3d framebuffer ABI");
_Static_assert(sizeof(C3D_RenderTarget) == 36, "citro3d render target ABI");
_Static_assert(offsetof(C3D_RenderTarget, linked) == 27, "citro2d inline scene helper ABI");
