#ifndef MP_AUDIO_MATH_H
#define MP_AUDIO_MATH_H
#include <stdint.h>
/* The DSP accumulator is wider than ARM11's 32-bit integer. Preserve the
 * predictor sum through rounding, then saturate to the output sample. */
static inline int16_t mp_adpcm_sample(int nibble,unsigned shift,int16_t c0,int16_t c1,int16_t y0,int16_t y1){
    int64_t sum=(int64_t)nibble*(1u<<shift)*2048+(int64_t)c0*y0+(int64_t)c1*y1+1024;
    int64_t value=sum>>11;
    return value>32767?32767:value< -32768?-32768:(int16_t)value;
}
/* Preserve AX's four-sample history and fractional source position across
 * blocks. Linear interpolation replaces the former sample-and-hold pitch
 * conversion; the original polyphase ROM filter is still not implemented. */
static inline void mp_src_block(int16_t*out,unsigned count,uint32_t ratio,uint32_t*phase,int16_t history[4],int nearest,int16_t(*read_sample)(void*),void*context){
    for(unsigned i=0;i<count;++i){
        if(nearest){int16_t sample=read_sample(context);for(unsigned j=0;j<3;++j)history[j]=history[j+1];history[3]=sample;out[i]=sample;continue;}
        *phase+=ratio;
        while(*phase>=65536){for(unsigned j=0;j<3;++j)history[j]=history[j+1];history[3]=read_sample(context);*phase-=65536;}
        int32_t fraction=*phase;
        out[i]=((int32_t)history[0]*(65536-fraction)+(int32_t)history[1]*fraction)>>16;
    }
}
#endif
