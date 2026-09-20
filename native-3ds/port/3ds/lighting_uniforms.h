/* GX emits channel enables as 0/1, light tags as 0/1/2, and attenuation as
 * GX_AF_SPEC/SPOT/NONE (0/1/2). These values are exactly representable in
 * both ARM float32 and PICA float24. They select control flow only. */
#include "unit_attenuation.h"
#ifdef MP_SMOKE_TEST
volatile unsigned unit_attenuation_disable=1;
#else
#define unit_attenuation_disable 0
#endif
unsigned unit_attenuation_lights,unit_attenuation_vertices;
static unsigned unit_attenuation_current;
static const char*const lighting_boolean_names[]={
    "constant_color","unlit_color",
    "light0_active","light1_active","light2_active","light3_active",
    "light0_channel1","light1_channel1","light2_channel1","light3_channel1",
    "light0_attenuate","light1_attenuate","light2_attenuate","light3_attenuate",
    "channel0_lit","channel1_lit"
};
static unsigned lighting_uniform_mask(const MPGPUUniforms*g){
    unsigned mask=0,atten[2];unit_attenuation_current=0;
    for(unsigned ch=0;ch<2;++ch){
        u32 enabled=read32(&g->value[MP_GPU_CONFIG+ch][0]);
        atten[ch]=read32(&g->value[MP_GPU_CONFIG+ch][3]);
#ifdef MP_SMOKE_TEST
        if((enabled!=0&&enabled!=0x3f800000)||
           (atten[ch]!=0&&atten[ch]!=0x3f800000&&atten[ch]!=0x40000000))
            mp_native_panic("Non-enum GX lighting configuration");
#endif
        if(enabled)mask|=1u<<(14+ch);
    }
    for(unsigned i=0;i<4;++i){
        u32 tag=read32(&g->value[MP_GPU_LIGHT_COLOR+i][3]);
#ifdef MP_SMOKE_TEST
        if(tag!=0&&tag!=0x3f800000&&tag!=0x40000000)
            mp_native_panic("Non-enum GX light channel");
#endif
        unsigned channel=tag==0x40000000;
        if(tag)mask|=1u<<(2+i);
        if(channel)mask|=1u<<(6+i);
        if(atten[channel]<0x40000000){
            if(tag&&!unit_attenuation_disable&&mp_unit_attenuation(g,i))++unit_attenuation_current;
            else mask|=1u<<(10+i);
        }
    }
    unit_attenuation_lights+=unit_attenuation_current;
    return mask;
}
