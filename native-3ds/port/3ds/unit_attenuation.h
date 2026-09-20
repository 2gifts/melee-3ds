#ifndef MP_UNIT_ATTENUATION_H
#define MP_UNIT_ATTENUATION_H
/* Wire words are BE8. The lighting expression is max(1 + 0*x + 0*x*x,0)
 * divided by (1 + 0*y + 0*y*y). For finite lighting intermediates this is
 * exactly one in float24, including the reciprocal and multiplication.
 * Signed zero coefficients are equivalent. Keep nonconstant attenuation
 * (including tiny distance terms) on the complete original path. */
static int mp_unit_attenuation(const MPGPUUniforms*g,unsigned light){
    const float*a=g->value[MP_GPU_ATTENUATION+light];
    const float*c=g->value[MP_GPU_COS_ATTENUATION+light];
    return read32(a)==0x3f800000&&read32(c)==0x3f800000&&
        !((read32(a+1)|read32(a+2)|read32(c+1)|read32(c+2))&0x7fffffff);
}
#endif
