#include "../port/engine/audio_math.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int16_t ramp(void*ctx){int*next=ctx;return 13*(*next)++;}
int main(void){
    const uint32_t ratios[]={0,1,16384,32768,49152,65535,65536,65537,98304,131072,147456,262144};
    unsigned tests=0;
    for(unsigned r=0;r<sizeof(ratios)/sizeof(*ratios);++r){
        int16_t a[96],b[96],h0[]={-39,-26,-13,0},h1[]={-39,-26,-13,0};uint32_t p0=0,p1=0;int n0=1,n1=1;
        mp_src_block(a,96,ratios[r],&p0,h0,0,ramp,&n0);
        for(unsigned i=0;i<96;++i){
            mp_src_block(b+i,1,ratios[r],&p1,h1,0,ramp,&n1);
            int expected=(int)(((uint64_t)(i+1)*ratios[r]*13)>>16)-39;
            assert(a[i]==expected);++tests;
        }
        assert(!memcmp(a,b,sizeof(a))&&!memcmp(h0,h1,sizeof(h0))&&p0==p1&&n0==n1);
    }
    int n=1;uint32_t phase=123;int16_t h[4]={0},out[96];
    mp_src_block(out,96,123456,&phase,h,1,ramp,&n);
    for(unsigned i=0;i<96;++i)assert(out[i]==13*(i+1));
    assert(phase==123&&h[0]==13*93&&h[3]==13*96);
    printf("AX source conversion: %u fractional samples, block continuity and direct mode passed\n",tests);
}
