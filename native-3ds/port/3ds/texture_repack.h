#ifndef MP_TEXTURE_REPACK_H
#define MP_TEXTURE_REPACK_H
#include <stdint.h>
#include "texture_decode.h"
#include "texture_storage.h"

typedef struct {
    const uint8_t *pixels, *palette;
    unsigned width,height,format,palette_format,palette_count;
} MPTextureSource;
static unsigned mp_repack_be16(const uint8_t*p){return ((unsigned)p[0]<<8)|p[1];}
static unsigned mp_repack_le16(const uint8_t*p){return p[0]|((unsigned)p[1]<<8);}
static void mp_repack_nibble(void*out,unsigned index,unsigned value){
    uint8_t*p=(uint8_t*)out+index/2;unsigned shift=(index&1)*4;
    *p=(*p&~(15u<<shift))|(value<<shift);
}
static uint32_t mp_repack_palette(const MPTextureSource*s,unsigned index,unsigned native){
    if(!s->palette||index>=s->palette_count)return 0xffffffffu;
    const uint8_t*p=s->palette+2*index;
    if(s->palette_format==1)return mp_repack_be16(p);
    if(s->palette_format==2)return mp_rgb5a3(mp_repack_be16(p));
    if(native==5)return mp_repack_le16(p);
    return mp_rgba(p[1],p[1],p[1],p[0]);
}

/* A native 8x8 Morton tile is four consecutive 4x4 Morton subtiles.
 * Move source-tile arithmetic outside the inner loop. Exact native formats
 * (RGB565, intensity and intensity/alpha) need no RGBA expansion/repacking.
 * Only partial/padded subtiles take the per-pixel edge-clamping path. */
#define MP_REPACK_TILES(BWX,BHY,BYTES,WRITE) do { \
    unsigned pitch=(s->width+(1u<<(BWX))-1)>>(BWX); \
    for(unsigned by=0;by<height;by+=4)for(unsigned bx=0;bx<width;bx+=4){ \
        unsigned outbase=((by>>3)*(width>>3)+(bx>>3))*64+((by&4)?32:0)+((bx&4)?16:0); \
        if(bx+4<=s->width&&by+4<=s->height){ \
            const uint8_t*p=s->pixels+((by>>(BHY))*pitch+(bx>>(BWX)))*(BYTES); \
            unsigned base=((by&((1u<<(BHY))-1))<<(BWX))+(bx&((1u<<(BWX))-1)); \
            for(unsigned q=0;q<16;++q){unsigned k=base+(my[q]<<(BWX))+mx[q],di=outbase+q;WRITE;} \
        }else{ \
            for(unsigned q=0;q<16;++q){unsigned x=bx+mx[q],y=by+my[q]; \
                if(x>=s->width)x=s->width-1;if(y>=s->height)y=s->height-1; \
                const uint8_t*p=s->pixels+((y>>(BHY))*pitch+(x>>(BWX)))*(BYTES); \
                unsigned k=((y&((1u<<(BHY))-1))<<(BWX))+(x&((1u<<(BWX))-1)),di=outbase+q;WRITE; \
            } \
        } \
    } \
}while(0)

#define MP_REPACK_PALETTE(INDEX) do { \
    unsigned idx=(INDEX);uint32_t value=idx<cached?palette[idx]:mp_repack_palette(s,idx,native); \
    if(native==0)((uint32_t*)out)[di]=value;else ((uint16_t*)out)[di]=(uint16_t)value; \
}while(0)

static int mp_texture_repack(void*out,unsigned width,unsigned height,const MPTextureSource*s){
    static const uint8_t mx[16]={0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3};
    static const uint8_t my[16]={0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3};
    uint32_t palette[256];unsigned cached=0,native=mp_texture_format(s->format,s->palette_format);
    if(!s->width||!s->height||width<s->width||height<s->height||(width&7)||(height&7))return 0;
    if(s->format>=8&&s->format<=10){
        cached=s->palette_count<256?s->palette_count:256;
        for(unsigned i=0;i<cached;++i)palette[i]=mp_repack_palette(s,i,native);
    }
    switch(s->format){
    case 0:MP_REPACK_TILES(3,3,32,mp_repack_nibble(out,di,(p[k/2]>>((k&1)?0:4))&15));break;
    case 1:MP_REPACK_TILES(3,2,32,((uint8_t*)out)[di]=p[k]);break;
    case 2:MP_REPACK_TILES(3,2,32,((uint8_t*)out)[di]=(p[k]<<4)|(p[k]>>4));break;
    case 3:MP_REPACK_TILES(2,2,32,((uint16_t*)out)[di]=mp_repack_le16(p+2*k));break;
    case 4:MP_REPACK_TILES(2,2,32,((uint16_t*)out)[di]=mp_repack_be16(p+2*k));break;
    case 5:MP_REPACK_TILES(2,2,32,((uint32_t*)out)[di]=mp_rgb5a3(mp_repack_be16(p+2*k)));break;
    case 6:MP_REPACK_TILES(2,2,64,((uint32_t*)out)[di]=mp_rgba(p[2*k+1],p[32+2*k],p[32+2*k+1],p[2*k]));break;
    case 8:MP_REPACK_TILES(3,3,32,MP_REPACK_PALETTE((p[k/2]>>((k&1)?0:4))&15));break;
    case 9:MP_REPACK_TILES(3,2,32,MP_REPACK_PALETTE(p[k]));break;
    case 10:MP_REPACK_TILES(2,2,32,MP_REPACK_PALETTE(mp_repack_be16(p+2*k)&0x3fff));break;
    default:return 0;
    }
    return 1;
}
#undef MP_REPACK_PALETTE
#undef MP_REPACK_TILES
#endif
