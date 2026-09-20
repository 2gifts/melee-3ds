#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../port/engine/gpu_vertex.h"
static uint32_t read32(const void*p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
#include "../port/3ds/unit_attenuation.h"
static void word(void*p,uint32_t v){v=__builtin_bswap32(v);memcpy(p,&v,4);}
int main(void){
    MPGPUUniforms u={0};unsigned checks=0;
    const unsigned nonzero[]={1,0x80000001,0x3f800000,0xbf800000,0x3f7fffff,0x7f800000,0xff800000,0x7fc00000};
    for(unsigned light=0;light<4;++light)for(unsigned signs=0;signs<16;++signs){
        float*a=u.value[MP_GPU_ATTENUATION+light],*c=u.value[MP_GPU_COS_ATTENUATION+light];
        word(a,0x3f800000);word(c,0x3f800000);
        void*coef[]={a+1,a+2,c+1,c+2};
        for(unsigned i=0;i<4;++i)word(coef[i],signs&(1u<<i)?0x80000000:0);
        assert(mp_unit_attenuation(&u,light));++checks;
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<sizeof(nonzero)/sizeof(*nonzero);++j){
            word(coef[i],nonzero[j]);assert(!mp_unit_attenuation(&u,light));++checks;
            word(coef[i],signs&(1u<<i)?0x80000000:0);
        }
        for(unsigned i=0;i<2;++i){void*p=i?(void*)a:(void*)c;
            for(unsigned j=0;j<sizeof(nonzero)/sizeof(*nonzero);++j)if(nonzero[j]!=0x3f800000){
                word(p,nonzero[j]);assert(!mp_unit_attenuation(&u,light));++checks;
            }
            word(p,0x3f800000);
        }
    }
    printf("Unit attenuation: %u BE8 predicate checks; signed zeros, tiny coefficients, adjacent floats and nonfinite values passed\n",checks);
}
