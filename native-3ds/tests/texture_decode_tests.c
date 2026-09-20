#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/3ds/texture_decode.h"
/* Reference expands a bit pattern by repeating its most significant bits,
 * and computes one source pixel independently of the tiled batch decoder. */
static unsigned expand(unsigned v,unsigned bits){
    unsigned result=0;for(unsigned i=0;i<8;++i)result=(result<<1)|((v>>(bits-1-i%bits))&1);return result;
}
static uint32_t reference(const uint8_t *src,unsigned width,unsigned x,unsigned y){
    unsigned tile=(y/8)*((width+7)/8)+x/8,sub=(y%8>=4)*2+(x%8>=4);
    const uint8_t *b=src+32*tile+8*sub;unsigned code=(b[4+y%4]>>(6-2*(x%4)))&3;
    unsigned a=b[0]*256+b[1],c=b[2]*256+b[3],alpha=(a<=c&&code==3)?0:255;
    unsigned offsets[]={11,5,0},bits[]={5,6,5};uint32_t out=0;
    for(unsigned i=0;i<3;++i){
        unsigned va=expand((a>>offsets[i])&((1<<bits[i])-1),bits[i]);
        unsigned vb=expand((c>>offsets[i])&((1<<bits[i])-1),bits[i]);
        unsigned v=code==0?va:code==1?vb:a<=c?(va+vb)/2:code==2?(5*va+3*vb)/8:(3*va+5*vb)/8;
        out=(out<<8)|v;
    }
    return(out<<8)|alpha;
}
int main(void){
    for(unsigned v=0;v<65536;++v){
        assert(mp_rgb565(v)==((expand(v>>11,5)<<24)|(expand((v>>5)&63,6)<<16)|(expand(v&31,5)<<8)|255));
        unsigned a=v&0x8000?255:expand((v>>12)&7,3);
        unsigned r=v&0x8000?expand((v>>10)&31,5):expand((v>>8)&15,4);
        unsigned g=v&0x8000?expand((v>>5)&31,5):expand((v>>4)&15,4);
        unsigned b=v&0x8000?expand(v&31,5):expand(v&15,4);
        assert(mp_rgb5a3(v)==((r<<24)|(g<<16)|(b<<8)|a));
    }
    unsigned random=0x434d5052,pixels=0;
    for(unsigned trial=0;trial<256;++trial){
        unsigned sw=1+(trial*37)%127,sh=1+(trial*59)%127,w=8,h=8;
        while(w<sw)w*=2;while(h<sh)h*=2;
        unsigned bytes=((sw+7)/8)*((sh+7)/8)*32;
        uint8_t *src=malloc(bytes);uint32_t *buffer=malloc((w*h+2)*4),*dst=buffer+1;
        for(unsigned i=0;i<bytes;++i){random=random*1664525+1013904223;src[i]=random>>24;}
        buffer[0]=buffer[w*h+1]=0xabcdef12;
        mp_cmpr_to_native(dst,w,h,src,sw,sh);
        for(unsigned i=0;i<w*h;++i){
            /* Decode destination tile address independently by deinterleaving. */
            unsigned tile=i/64,k=i%64,x=(tile%(w/8))*8,y=(tile/(w/8))*8;
            for(unsigned bit=0;bit<3;++bit){x|=((k>>(2*bit))&1)<<bit;y|=((k>>(2*bit+1))&1)<<bit;}
            if(x>=sw)x=sw-1;if(y>=sh)y=sh-1;
            assert(dst[i]==reference(src,sw,x,y));++pixels;
        }
        assert(buffer[0]==0xabcdef12&&buffer[w*h+1]==0xabcdef12);free(buffer);free(src);
    }
    /* Explicit transparent midpoint preserves RGB for bilinear filtering. */
    uint8_t block[8]={0,0,255,255,255,255,255,255};uint32_t colors[4];mp_cmpr_palette(block,colors);
    assert(colors[3]==0x7f7f7f00);
    puts("All 131072 RGB565/RGB5A3 words match bit replication");
    printf("CMPR: %u pixels across 256 padded/tiled textures match the independent GameCube reference\n",pixels);
    return 0;
}
