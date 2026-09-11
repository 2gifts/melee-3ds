#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../port/3ds/texture_repack.h"
typedef uint32_t u32;
typedef uint8_t u8;
typedef struct {uintptr_t image,palette;unsigned format,w,h,palcount,palfmt;} Draw;
static unsigned read16(const void*p){const uint8_t*b=p;return b[0]*256+b[1];}
static void mp_native_panic(const char*s){fprintf(stderr,"%s\n",s);abort();}
#include "texture_reference.inc"
static void reference(void*out,unsigned w,unsigned h,const MPTextureSource*s){
    Draw d={(uintptr_t)s->pixels,(uintptr_t)s->palette,s->format,s->width,s->height,s->palette_count,s->palette_format};
    unsigned format=mp_texture_format(s->format,s->palette_format);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        unsigned tx=x<s->width?x:s->width-1,ty=y<s->height?y:s->height-1;
        mp_texture_store(out,((y/8)*(w/8)+x/8)*64+morton(x,y),format,decode(&d,tx,ty));
    }
}
static uint32_t rng=0x72706163;
static uint8_t random_byte(void){rng=rng*1664525+1013904223;return rng>>24;}
int main(void){
    unsigned formats[]={0,1,2,3,4,5,6,8,9,10},counts[]={0,1,15,16,255,256,257,16384};
    unsigned cases=0;uint64_t pixels=0;
    for(unsigned fi=0;fi<10;++fi)for(unsigned trial=0;trial<96;++trial){
        unsigned sw=1+trial*37%129,sh=1+trial*59%131,w=8,h=8,fmt=formats[fi],pf=trial%4;
        if(trial==94){sw=640;sh=406;}if(trial==95){sw=250;sh=160;}
        while(w<sw)w*=2;while(h<sh)h*=2;
        unsigned bw=4,bh=4,block=fmt==6?64:32;
        if(fmt==0||fmt==8)bw=bh=8;else if(fmt==1||fmt==2||fmt==9)bw=8;
        unsigned source_bytes=((sw+bw-1)/bw)*((sh+bh-1)/bh)*block;
        unsigned pc=counts[trial%8],bytes=w*h*mp_texture_bits(mp_texture_format(fmt,pf))/8;
        uint8_t*allocation=malloc(source_bytes+7),*src=allocation+trial%8,*pal=malloc(2*pc+1);
        uint8_t*a=malloc(bytes+32),*b=malloc(bytes+32);
        for(unsigned i=0;i<source_bytes;++i)src[i]=random_byte();
        for(unsigned i=0;i<pc*2;++i)pal[1+i]=random_byte();
        memset(a,0xa5,bytes+32);memset(b,0xa5,bytes+32);
        MPTextureSource s={src,trial%11?pal+1:NULL,sw,sh,fmt,pf,pc};
        assert(mp_texture_repack(a+16,w,h,&s));reference(b+16,w,h,&s);
        if(memcmp(a,b,bytes+32)){fprintf(stderr,"Mismatch fmt=%u palette=%u trial=%u size=%ux%u\n",fmt,pf,trial,sw,sh);return 1;}
        for(unsigned i=0;i<16;++i)assert(a[i]==0xa5&&a[bytes+16+i]==0xa5);
        ++cases;pixels+=(uint64_t)w*h;
        free(a);free(b);free(pal);free(allocation);
    }
    printf("Texture repack: %u cases and %llu pixels match the original renderer, including all 10 formats, palettes and padded edges\n",cases,(unsigned long long)pixels);
    unsigned sw=640,sh=406,w=1024,h=512;
    uint8_t*src=malloc(sw*((sh+3)&~3)*2),*out=malloc(w*h*2);
    memset(src,0x5b,sw*((sh+3)&~3)*2);MPTextureSource s={src,NULL,sw,sh,4,0,0};
    clock_t begin=clock();for(unsigned i=0;i<24;++i)reference(out,w,h,&s);clock_t middle=clock();
    for(unsigned i=0;i<24;++i)assert(mp_texture_repack(out,w,h,&s));clock_t end=clock();
    printf("Host RGB565 conversion benchmark: reference %.3f ms, repack %.3f ms per image (not console FPS)\n",1000.*(middle-begin)/CLOCKS_PER_SEC/24,1000.*(end-middle)/CLOCKS_PER_SEC/24);
    free(src);free(out);
}
