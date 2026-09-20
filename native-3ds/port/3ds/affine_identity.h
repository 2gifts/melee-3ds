#ifndef MP_AFFINE_IDENTITY_H
#define MP_AFFINE_IDENTITY_H
/* Match the original affine expression to primary RGBA followed by clamp.
 * Vertex primary/material colors originate from GX bytes; the existing
 * shader clamps illumination before multiplying them. No vertex geometry,
 * lighting or texture arithmetic is replaced by this classification. */
static int mp_affine_identity(const MPGPUUniforms*g){
    for(unsigned j=0;j<4;++j){
        if(read32(&g->value[MP_GPU_CLAMP][j])!=0x3f800000)return 0;
        if((read32(&g->value[MP_GPU_SHADE][j])&0x7fffffff)||
           (read32(&g->value[MP_GPU_SHADE+3][j])&0x7fffffff)||
           (read32(&g->value[MP_GPU_SHADE+4][j])&0x7fffffff))return 0;
        u32 x=read32(&g->value[MP_GPU_SHADE+1][j]);
        u32 a=read32(&g->value[MP_GPU_SHADE+2][j]);
        if(x==0x3f800000&&!(a&0x7fffffff))continue;
        if(j==3&&a==0x3f800000&&!(x&0x7fffffff))continue;
        return 0;
    }
    return 1;
}
#endif
