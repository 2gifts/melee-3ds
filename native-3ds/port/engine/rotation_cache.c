#include <math.h>
#include <dolphin/types.h>
#include "native.h"

/* Repeated joint angles use the exact existing libm results. No angle
 * quantization, lookup-table interpolation or animation-rate reduction. */
typedef union{float f;u32 u;} FloatBits;
typedef struct{u32 key,mode,valid;float sine,cosine;} Rotation;
static Rotation rotations[512];
volatile unsigned mp_rotation_cache=1,mp_rotation_cache_validate;
unsigned mp_rotation_hits,mp_rotation_misses,mp_rotation_zeros,mp_rotation_checks;

void mp_rotation_sincos(float angle,float*sine,float*cosine){
    FloatBits key={.f=angle};u32 mode;
    __asm__ volatile("vmrs %0,fpscr":"=r"(mode));
    mode&=0x07f70000; /* Rounding, flush/default-NaN and vector control. */
    Rotation*r=&rotations[((key.u^(key.u>>13))*0x9e3779b9u)>>23];
    if(!mp_rotation_cache||(key.u&0x7f800000)==0x7f800000){
        *sine=sinf(angle);*cosine=cosf(angle);return;
    }
    if(!(key.u&0x7fffffff)){
        *sine=angle;*cosine=1.f;++mp_rotation_zeros;
    }else if(r->valid&&r->key==key.u&&r->mode==mode){
        *sine=r->sine;*cosine=r->cosine;++mp_rotation_hits;
    }else{
        *sine=sinf(angle);*cosine=cosf(angle);
        r->key=key.u;r->mode=mode;r->sine=*sine;r->cosine=*cosine;r->valid=1;
        ++mp_rotation_misses;
    }
    if(mp_rotation_cache_validate){
        FloatBits a={.f=*sine},b={.f=*cosine},x={.f=sinf(angle)},y={.f=cosf(angle)};
        if(a.u!=x.u||b.u!=y.u)mp_platform_panic("Cached joint rotation differs from original libm");
        ++mp_rotation_checks;
    }
}

/* Reachable only through the development build's test bridge; release link
 * garbage collection removes this fixture. Test the actual ARM libm and
 * floating-point controls, including signed zero and extreme arguments. */
unsigned mp_rotation_self_test(void){
    static const u32 cases[]={0,0x80000000,1,0x80000001,0x007fffff,
        0x00800000,0x3f800000,0xbf800000,0x40490fdb,0xc0490fdb,
        0x3fc90fdb,0x3f000000,0x7f7fffff,0xff7fffff,0x7f800000,
        0xff800000,0x7fc00001,0x7fa00001};
    u32 saved,seed=0x31415926;unsigned count=0;
    unsigned enabled=mp_rotation_cache,validate=mp_rotation_cache_validate;
    __asm__ volatile("vmrs %0,fpscr":"=r"(saved));
    mp_rotation_cache=1;mp_rotation_cache_validate=0;
    for(unsigned mode=0;mode<16;++mode){
        u32 control=(saved&~0x03c00000)|((mode&3)<<22)|((mode&12)<<22);
        __asm__ volatile("vmsr fpscr,%0"::"r"(control):"memory");
        for(unsigned i=0;i<530;++i){
            seed=seed*1664525+1013904223;
            FloatBits key={.u=i<sizeof(cases)/sizeof(*cases)?cases[i]:seed};
            FloatBits x={.f=sinf(key.f)},y={.f=cosf(key.f)},a,b;
            for(unsigned repeat=0;repeat<2;++repeat){
                mp_rotation_sincos(key.f,&a.f,&b.f);
                if(a.u!=x.u||b.u!=y.u)mp_platform_panic("Joint rotation self-test mismatch");
                ++count;
            }
        }
    }
    __asm__ volatile("vmsr fpscr,%0"::"r"(saved):"memory");
    mp_rotation_cache=enabled;mp_rotation_cache_validate=validate;
    return count;
}
