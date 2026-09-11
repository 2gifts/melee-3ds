#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/3ds/efb_rgb565.h"
static uint32_t source[400*240],seed=0x87cd1;
static uint32_t random_word(void){seed=seed*1664525+1013904223;return seed;}
static unsigned cases;static unsigned long long pixels;
static void reference(uint8_t*dst,unsigned w,unsigned h,unsigned left,unsigned top,unsigned sw,unsigned sh,unsigned screen){
    unsigned pitch=(w+3)/4;memset(dst,0,pitch*((h+3)/4)*32);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        unsigned sx=(400-screen)/2+(left+x*sw/w)*screen/640,sy=(top+y*sh/h)*240/480;
        if(sx>=400)sx=399;if(sy>=240)sy=239;
        uint32_t c=source[sx*240+239-sy];
        unsigned r=c>>24,g=(c>>16)&255,b=(c>>8)&255,v=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
        unsigned offset=((y/4)*pitch+x/4)*32+2*((y%4)*4+x%4);
        dst[offset]=v>>8;dst[offset+1]=v;
    }
}
static void check(unsigned w,unsigned h,unsigned left,unsigned top,unsigned sw,unsigned sh,unsigned screen){
    unsigned bytes=((w+3)/4)*((h+3)/4)*32;
    uint8_t*allocation=malloc(bytes+64),*expected=malloc(bytes);assert(allocation&&expected);
    memset(allocation,0xcd,bytes+64);uint8_t*actual=allocation+32;
    reference(expected,w,h,left,top,sw,sh,screen);
    assert(mp_efb_rgb565(actual,source,w,h,left,top,sw,sh,screen));
    assert(!memcmp(actual,expected,bytes));
    for(unsigned i=0;i<32;++i)assert(allocation[i]==0xcd&&actual[bytes+i]==0xcd);
    ++cases;pixels+=(unsigned long long)w*h;free(expected);free(allocation);
}
int main(void){
    unsigned dims[][2]={{1,1},{2,3},{3,2},{4,4},{7,9},{124,80},{250,160},{320,240},{640,406},{640,480},{1023,1021},{1024,1024}};
    for(unsigned i=0;i<400*240;++i)source[i]=random_word();
    for(unsigned i=0;i<sizeof(dims)/sizeof(*dims);++i)for(unsigned wide=0;wide<2;++wide){
        unsigned w=dims[i][0],h=dims[i][1],screen=wide?400:320;
        check(w,h,0,0,w,h,screen);check(w,h,0,36,w,h,screen);
        check(w,h,13,17,319,239,screen);check(w,h,639,479,1024,1024,screen);
    }
    for(unsigned i=0;i<256;++i){
        unsigned w=1+(random_word()>>8)%1024,h=1+(random_word()>>8)%512;
        check(w,h,(random_word()>>8)%700,(random_word()>>8)%500,1+(random_word()>>8)%1024,1+(random_word()>>8)%1024,i&1?400:320);
    }
    uint8_t guard[128];memset(guard,0xcd,sizeof(guard));
    for(unsigned offset=1;offset<4;++offset)assert(!mp_efb_rgb565(guard+offset,source,4,4,0,0,4,4,320));
    assert(!mp_efb_rgb565(guard,source,1025,1,0,0,1,1,320));
    assert(!mp_efb_rgb565(guard,source,1,1025,0,0,1,1,320));
    assert(!mp_efb_rgb565(guard,source,0,1,0,0,1,1,320));
    for(unsigned i=0;i<sizeof(guard);++i)assert(guard[i]==0xcd);
    printf("RGB565 EFB copy: %u cases, %llu exact pixels; padding, crop, clamping, 4:3/expanded views and destination guards passed\n",cases,pixels);
}
