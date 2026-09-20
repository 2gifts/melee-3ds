#ifndef MP_STEREO_FIXED_H
#define MP_STEREO_FIXED_H
#include <stdint.h>
#include <string.h>
/* Match the SDK's packed fixed attribute value before supplying an identical
 * parameter through a float32 uniform. In particular, an emulator must not
 * retain seven extra mantissa bits in the geometry shader's eye offsets.
 * f32tof24 is supplied by libctru, also used by C3D_ImmSendAttrib. */
static float mp_stereo_fixed_float(float value){
    uint32_t packed=f32tof24(value),sign=(packed&0x800000u)<<8;
    uint32_t mantissa=packed&0xffffu,exponent=(packed>>16)&127u,bits;
    if(exponent==127)bits=sign|0x7f800000u|(mantissa<<7);
    else if(!exponent){
        if(!mantissa)bits=sign;
        else{
            exponent=65;
            while(!(mantissa&0x10000u)){--exponent;mantissa<<=1;}
            bits=sign|(exponent<<23)|((mantissa&0xffffu)<<7);
        }
    }else bits=sign|((exponent+64u)<<23)|(mantissa<<7);
    float result;memcpy(&result,&bits,4);return result;
}
#endif
