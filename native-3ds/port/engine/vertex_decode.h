#ifndef MP_VERTEX_DECODE_H
#define MP_VERTEX_DECODE_H
#include <stdint.h>
static float mp_vertex_float(const unsigned char*p){
    union{uint32_t u;float f;}v={(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]};return v.f;
}
/* The format switch runs once per attribute, and fixed-point division is
 * folded into a scale once per primitive. Unaligned direct FIFO data is valid. */
static void mp_vertex_components(float*out,const unsigned char*p,unsigned type,unsigned n,float scale){
    switch(type){
    case 0:for(unsigned i=0;i<n;++i)out[i]=p[i]*scale;break;
    case 1:for(unsigned i=0;i<n;++i)out[i]=(int8_t)p[i]*scale;break;
    case 2:for(unsigned i=0;i<n;++i)out[i]=((p[2*i]<<8)|p[2*i+1])*scale;break;
    case 3:for(unsigned i=0;i<n;++i)out[i]=(int16_t)((p[2*i]<<8)|p[2*i+1])*scale;break;
    default:out[0]=mp_vertex_float(p);if(n>1)out[1]=mp_vertex_float(p+4);if(n>2)out[2]=mp_vertex_float(p+8);break;
    }
}
#endif
