#include "../port/engine/audio_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
    const int16_t extremes[]={-32768,-32767,-2048,-1,0,1,2048,32766,32767};unsigned tested=0;
    /* Use exact double arithmetic and floor as an independent reference to
     * the signed integer shift, including sums that overflow signed int32. */
    for(int n=-8;n<=7;++n)for(unsigned s=0;s<16;++s)
    for(unsigned a=0;a<9;++a)for(unsigned b=0;b<9;++b)
    for(unsigned c=0;c<9;++c)for(unsigned d=0;d<9;++d){
        double sum=n*(double)(1u<<s)*2048+extremes[a]*(double)extremes[c]+extremes[b]*(double)extremes[d]+1024;
        double expected=floor(sum/2048);if(expected>32767)expected=32767;if(expected< -32768)expected=-32768;
        assert(mp_adpcm_sample(n,s,extremes[a],extremes[b],extremes[c],extremes[d])==(int)expected);++tested;
    }
    printf("ADPCM predictor saturation: %u cases passed\n",tested);
}
