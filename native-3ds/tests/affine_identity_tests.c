#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../port/engine/gpu_vertex.h"
typedef uint32_t u32;
static uint32_t read32(const void*p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
#include "../port/3ds/affine_identity.h"
static void word(void*p,uint32_t v){v=__builtin_bswap32(v);memcpy(p,&v,4);}
static void identity(MPGPUUniforms*u,unsigned alpha,unsigned sign){
    memset(u,0,sizeof(*u));
    for(unsigned k=0;k<4;++k){
        word(&u->value[MP_GPU_CLAMP][k],0x3f800000);
        for(unsigned row=0;row<5;++row)word(&u->value[MP_GPU_SHADE+row][k],sign);
        word(&u->value[MP_GPU_SHADE+1+(alpha&&k==3)][k],0x3f800000);
    }
}
int main(void){
    const uint32_t bad[]={0,0x80000000,1,0x80000001,0x3f800000,0x3f7fffff,
        0x3f800001,0xbf800000,0x7f800000,0xff800000,0x7fc00000};
    unsigned accepted=0,rejected=0;MPGPUUniforms u;
    for(unsigned alpha=0;alpha<2;++alpha)for(unsigned negative=0;negative<2;++negative){
        identity(&u,alpha,negative?0x80000000:0);
        assert(mp_affine_identity(&u));++accepted;
        for(unsigned r=0;r<6;++r)for(unsigned k=0;k<4;++k){
            void*p=&u.value[r==5?MP_GPU_CLAMP:MP_GPU_SHADE+r][k];uint32_t old=read32(p);
            for(unsigned b=0;b<sizeof(bad)/sizeof(*bad);++b){
                if(bad[b]==old||(!(old&0x7fffffff)&&!(bad[b]&0x7fffffff)))continue;
                word(p,bad[b]);assert(!mp_affine_identity(&u));++rejected;
            }
            word(p,old);
        }
    }
    printf("Affine identity: %u identity forms and %u rejected BE8 coefficient mutations passed\n",accepted,rejected);
}
