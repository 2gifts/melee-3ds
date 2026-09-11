#include <citro3d.h>
#include <stddef.h>
#include "vendor/citro3d_internal.h"
#include "citro3d_fix.h"
/* The pinned SDK's C3D_TexBind reads tex->param before checking for NULL
 * on units 1/2. Use its normal dirty-state path when disabling those units.
 * These offsets were checked against the linked C3D_TexBind ARM code. */
_Static_assert(offsetof(C3D_Context,flags)==0x20,"Pinned Citro3D flags ABI");
_Static_assert(offsetof(C3D_Context,tex)==0x118,"Pinned Citro3D texture ABI");
void mp_native_tex_bind(int unit,C3D_Tex* texture){
    if(unit<0||unit>2)return;
    if(texture||unit==0){C3D_TexBind(unit,texture);return;}
    C3D_Context*ctx=C3Di_GetContext();
    if(!(ctx->flags&C3DiF_Active))return;
    ctx->flags|=C3DiF_Tex(unit);ctx->tex[unit]=NULL;
}
