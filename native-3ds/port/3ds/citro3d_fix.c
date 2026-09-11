#include <citro3d.h>
#include <stddef.h>
#include <string.h>
#include "vendor/citro3d_internal.h"
#include "citro3d_fix.h"
/* The pinned SDK's C3D_TexBind reads tex->param before checking for NULL
 * on units 1/2. Use its normal dirty-state path when disabling those units.
 * These offsets were checked against the linked C3D_TexBind ARM code. */
_Static_assert(offsetof(C3D_Context,flags)==0x20,"Pinned Citro3D flags ABI");
_Static_assert(offsetof(C3D_Context,tex)==0x118,"Pinned Citro3D texture ABI");
_Static_assert(sizeof(C3D_Tex)==24,"Texture binding snapshot ABI");
static C3D_Tex bound_texture[3];
static unsigned bound_valid;
#ifdef MP_SMOKE_TEST
volatile unsigned texture_bind_disable;
unsigned texture_bind_hits,texture_bind_updates;
#endif
void mp_native_tex_bind_invalidate(void){bound_valid=0;}
void mp_native_tex_bind(int unit,C3D_Tex* texture){
    if(unit<0||unit>2)return;
    C3D_Context*ctx=C3Di_GetContext();
    if(!(ctx->flags&C3DiF_Active))return;
    if(texture&&C3D_TexGetType(texture)!=GPU_TEX_2D){
        bound_valid&=~(1u<<unit);
        if(unit==0)C3D_TexBind(unit,texture);
        return;
    }
    /* Every dirty texture unit makes Citro3D clear PICA's texture cache,
     * including redundantly disabling an already disabled second map.
     * Compare the descriptor contents, not just the mutable object pointer.
     * Keep any pending library dirty bit intact when the binding is equal. */
    if(ctx->tex[unit]==texture&&(bound_valid&(1u<<unit))&&
       (!texture||!memcmp(&bound_texture[unit],texture,sizeof(*texture)))
#ifdef MP_SMOKE_TEST
       &&!texture_bind_disable
#endif
    ){
#ifdef MP_SMOKE_TEST
        ++texture_bind_hits;
#endif
        return;
    }
    if(texture)bound_texture[unit]=*texture;
    bound_valid|=1u<<unit;
#ifdef MP_SMOKE_TEST
    ++texture_bind_updates;
#endif
    ctx->flags|=C3DiF_Tex(unit);ctx->tex[unit]=texture;
}

#ifdef MP_SMOKE_TEST
volatile unsigned draw_packet_disable,draw_packet_validate;
unsigned draw_packet_checks,draw_packet_draws;
#endif
extern void mp_native_panic(const char*);
void mp_native_draw_elements(GPU_Primitive_t primitive,int count,int type,const void*indices){
    if(primitive!=GPU_TRIANGLES
#ifdef MP_SMOKE_TEST
       ||draw_packet_disable
#endif
    ){C3D_DrawElements(primitive,count,type,indices);return;}
    C3D_Context*ctx=C3Di_GetContext();
    u32 pa=osConvertVirtToPhys(indices),base=ctx->bufInfo.base_paddr;
    if(pa<base)return;
    C3Di_UpdateContext();
    /* Same command sequence as the pinned Citro3D drawElements.c, batched
     * into one checked packet. Keep primitive restart, draw-mode toggles
     * and post-vertex cache flushes exactly as the original SDK emits them. */
#define REG(reg,mask,value) value,GPUCMD_HEADER(0,mask,reg)
    u32 packet[]={
        REG(GPUREG_PRIMITIVE_CONFIG,2,GPU_GEOMETRY_PRIM),
        REG(GPUREG_RESTART_PRIMITIVE,15,1),
        REG(GPUREG_INDEXBUFFER_CONFIG,15,(pa-base)|((u32)type<<31)),
        REG(GPUREG_NUMVERTICES,15,(u32)count),
        REG(GPUREG_VERTEX_OFFSET,15,0),
        REG(GPUREG_GEOSTAGE_CONFIG,2,0x100),
        REG(GPUREG_GEOSTAGE_CONFIG2,2,0x100),
        REG(GPUREG_START_DRAW_FUNC0,1,0),
        REG(GPUREG_DRAWELEMENTS,15,1),
        REG(GPUREG_START_DRAW_FUNC0,1,1),
        REG(GPUREG_GEOSTAGE_CONFIG,2,0),
        REG(GPUREG_GEOSTAGE_CONFIG2,2,0),
        REG(GPUREG_VTX_FUNC,15,1),
        REG(GPUREG_PRIMITIVE_CONFIG,8,0),
        REG(GPUREG_PRIMITIVE_CONFIG,8,0),
    };
#undef REG
    unsigned words=sizeof(packet)/sizeof(*packet);
    if(gpuCmdBufOffset>gpuCmdBufSize||words>gpuCmdBufSize-gpuCmdBufOffset)
        mp_native_panic("GPU draw packet capacity exceeded");
#ifdef MP_SMOKE_TEST
    if(draw_packet_validate){
        unsigned start=gpuCmdBufOffset;
        C3D_DrawElements(primitive,count,type,indices);
        if(gpuCmdBufOffset-start!=words||memcmp(gpuCmdBuf+start,packet,sizeof(packet)))
            mp_native_panic("Batched draw differs from original Citro3D commands");
        gpuCmdBufOffset=start;++draw_packet_checks;
    }
    ++draw_packet_draws;
#endif
    GPUCMD_AddRawCommands(packet,words);
    ctx->flags|=C3DiF_DrawUsed;
}
