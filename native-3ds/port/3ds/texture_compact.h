#ifndef MP_TEXTURE_COMPACT_H
#define MP_TEXTURE_COMPACT_H
/* GX RGB5A3 and CMPR textures (and RGB5A3 palettes) decode to 32-bit RGBA,
 * but most need no more than 16 bits, which halves texture memory, the GPU's
 * texture reads and what VRAM promotion must copy:
 *   every texel opaque          -> RGB565  (RGB555 sources are exact)
 *   alpha only fully on or off  -> RGBA5551 (exact for RGB555/transparent)
 *   any partial alpha           -> RGBA8 as before
 * CMPR's blended palette entries (5:3 and 1:1 mixes of RGB565 endpoints) are
 * rounded to the nearest 5/6-bit value, within half a step of the original.
 * GX RGBA8 textures keep all their precision and are not candidates. */
#ifdef MP_SMOKE_TEST
volatile unsigned texture_compact_disable; /* Development comparison. */
#else
#define texture_compact_disable 0
#endif
unsigned texture_compact_counts[3]; /* kept RGBA8, RGB565, RGBA5551 */
unsigned texture_compact_saved_bytes;
static int texture_compact_candidate(const Draw*d){
    return !texture_compact_disable&&(d->format==14||d->format==5||(d->format>=8&&d->format<=10&&d->palfmt==2));
}
static uint32_t*texture_compact_scratch(unsigned texels){
    static uint32_t*buffer;static unsigned capacity;
    if(texels>capacity){free(buffer);buffer=malloc(texels*4);capacity=buffer?texels:0;}
    return buffer;
}
static unsigned texture_compact_format(const uint32_t*rgba,unsigned n){
    unsigned opaque=1;
    for(unsigned i=0;i<n;++i){unsigned a=rgba[i]&255;
        if(a==255)continue;
        if(a){++texture_compact_counts[0];return GPU_RGBA8;}
        opaque=0;
    }
    ++texture_compact_counts[opaque?1:2];texture_compact_saved_bytes+=n*2;
    return opaque?GPU_RGB565:GPU_RGBA5551;
}
static unsigned texture_q5(unsigned v){return (v*31+127)/255;}
static unsigned texture_q6(unsigned v){return (v*63+127)/255;}
static void texture_compact_store(void*out,const uint32_t*rgba,unsigned n,unsigned format){
    if(format==GPU_RGBA8){memcpy(out,rgba,n*4);return;}
    uint16_t*o=out;
    for(unsigned i=0;i<n;++i){uint32_t v=rgba[i];unsigned r=v>>24,g=(v>>16)&255,b=(v>>8)&255;
        o[i]=format==GPU_RGB565?(texture_q5(r)<<11)|(texture_q6(g)<<5)|texture_q5(b):
            (texture_q5(r)<<11)|(texture_q5(g)<<6)|(texture_q5(b)<<1)|((v&255)?1:0);}
}
#endif
