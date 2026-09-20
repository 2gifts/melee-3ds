#include <citro3d.h>
#include <string.h>
_Static_assert(sizeof(bool)==1&&C3D_FVUNIF_COUNT==96&&C3D_IVUNIF_COUNT==4,"Pinned uniform dirty-array ABI");

/* Citro3D's context update checks both shader stages for every draw, including
 * the second stereo eye. Most of those stages have no uniforms to upload.
 * Keep the SDK uploader and its private shader-constant/restore bookkeeping;
 * only bypass it when every public dirty flag and pending event is clear. */
static unsigned pending[2]={1,1};
extern void __real_C3D_UpdateUniforms(GPU_SHADER_TYPE);
extern void __real_C3Di_LoadShaderUniforms(shaderInstance_s*);
extern void __real_C3Di_ClearShaderUniforms(GPU_SHADER_TYPE);
extern void __real_C3Di_DirtyUniforms(GPU_SHADER_TYPE);

#ifdef MP_SMOKE_TEST
volatile unsigned uniform_dispatch_disable,uniform_dispatch_validate;
unsigned uniform_dispatch_calls[2],uniform_dispatch_skips[2],uniform_dispatch_checks;
extern void mp_native_panic(const char*);
#endif

static int any_float_dirty(GPU_SHADER_TYPE type)
{
    /* memcpy is defined for the SDK's bool arrays regardless of alignment and
     * aliasing. Clang emits word loads on ARM; zero has the same byte layout. */
    for(unsigned i=0;i<C3D_FVUNIF_COUNT;i+=4){
        u32 word;memcpy(&word,&C3D_FVUnifDirty[type][i],sizeof(word));
        if(word)return 1;
    }
    return 0;
}

void __wrap_C3D_UpdateUniforms(GPU_SHADER_TYPE type)
{
    u32 integer_dirty;memcpy(&integer_dirty,C3D_IVUnifDirty[type],sizeof(integer_dirty));
#ifdef MP_SMOKE_TEST
    ++uniform_dispatch_calls[type];
    if(uniform_dispatch_disable){__real_C3D_UpdateUniforms(type);pending[type]=0;return;}
#endif
    if(pending[type]||C3D_BoolUnifsDirty[type]||integer_dirty||any_float_dirty(type)){
        __real_C3D_UpdateUniforms(type);pending[type]=0;return;
    }
#ifdef MP_SMOKE_TEST
    ++uniform_dispatch_skips[type];
    if(uniform_dispatch_validate){
        unsigned start=gpuCmdBufOffset;
        __real_C3D_UpdateUniforms(type);
        if(gpuCmdBufOffset!=start)mp_native_panic("Skipped uniform upload produced SDK commands");
        ++uniform_dispatch_checks;
    }
#endif
}

void __wrap_C3Di_LoadShaderUniforms(shaderInstance_s* shader)
{
    pending[shader->dvle->type]=1;
    __real_C3Di_LoadShaderUniforms(shader);
}

void __wrap_C3Di_ClearShaderUniforms(GPU_SHADER_TYPE type)
{
    pending[type]=1;
    __real_C3Di_ClearShaderUniforms(type);
}

void __wrap_C3Di_DirtyUniforms(GPU_SHADER_TYPE type)
{
    pending[type]=1;
    __real_C3Di_DirtyUniforms(type);
}
