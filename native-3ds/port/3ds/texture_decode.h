#ifndef MP_TEXTURE_DECODE_H
#define MP_TEXTURE_DECODE_H
#include <stdint.h>
static uint32_t mp_rgba(unsigned r,unsigned g,unsigned b,unsigned a){return(r<<24)|(g<<16)|(b<<8)|a;}
static unsigned mp_expand3(unsigned x){return(x<<5)|(x<<2)|(x>>1);}
static unsigned mp_expand5(unsigned x){return(x<<3)|(x>>2);}
static unsigned mp_expand6(unsigned x){return(x<<2)|(x>>4);}
static uint32_t mp_rgb565(unsigned v){return mp_rgba(mp_expand5(v>>11),mp_expand6((v>>5)&63),mp_expand5(v&31),255);}
static uint32_t mp_rgb5a3(unsigned v){
    return v&0x8000?mp_rgba(mp_expand5((v>>10)&31),mp_expand5((v>>5)&31),mp_expand5(v&31),255):
        mp_rgba(((v>>8)&15)*17,((v>>4)&15)*17,(v&15)*17,mp_expand3((v>>12)&7));
}
static uint32_t mp_cmpr_mix(uint32_t a,uint32_t b,unsigned wa,unsigned wb,unsigned shift){
    return mp_rgba((((a>>24)&255)*wa+((b>>24)&255)*wb)>>shift,
        (((a>>16)&255)*wa+((b>>16)&255)*wb)>>shift,
        (((a>>8)&255)*wa+((b>>8)&255)*wb)>>shift,255);
}
static void mp_cmpr_palette(const uint8_t *block,uint32_t color[4]){
    unsigned a=(block[0]<<8)|block[1],b=(block[2]<<8)|block[3];
    color[0]=mp_rgb565(a);color[1]=mp_rgb565(b);
    /* GameCube CMPR uses 5:3 interpolation, and its transparent entry
     * retains the average RGB. These differ from ordinary DXT1 decoding. */
    if(a>b){color[2]=mp_cmpr_mix(color[0],color[1],5,3,3);color[3]=mp_cmpr_mix(color[0],color[1],3,5,3);}
    else{color[2]=mp_cmpr_mix(color[0],color[1],1,1,1);color[3]=color[2]&0xffffff00;}
}
static unsigned mp_texture_morton(unsigned x,unsigned y){return(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);}
static void mp_cmpr_to_native(uint32_t *dst,unsigned width,unsigned height,const uint8_t *src,unsigned source_width,unsigned source_height){
    unsigned pitch=(source_width+7)/8;
    /* Decode endpoints once per 4x4 sub-block, directly into native tiles.
     * Pad beyond the original dimensions by repeating the edge texel. */
    for(unsigned by=0;by<height;by+=4)for(unsigned bx=0;bx<width;bx+=4){
        unsigned sx=bx<source_width?bx:source_width-1,sy=by<source_height?by:source_height-1;
        const uint8_t *block=src+((sy/8)*pitch+sx/8)*32+((sy%8)/4*2+(sx%8)/4)*8;
        uint32_t color[4];mp_cmpr_palette(block,color);
        for(unsigned y=by;y<by+4;++y)for(unsigned x=bx;x<bx+4;++x){
            unsigned tx=x<source_width?x:source_width-1,ty=y<source_height?y:source_height-1;
            unsigned selector=(block[4+ty%4]>>(6-2*(tx%4)))&3;
            dst[((y/8)*(width/8)+x/8)*64+mp_texture_morton(x,y)]=color[selector];
        }
    }
}
#endif
