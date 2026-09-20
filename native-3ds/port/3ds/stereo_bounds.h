#ifndef MP_STEREO_BOUNDS_H
#define MP_STEREO_BOUNDS_H
/* Experimental CPU eligibility test for stereo vertex reuse. Not connected
 * to the production renderer yet. Fail closed for non-finite/extreme inputs.
 * Intervals cover each bone independently and both projection stages; an
 * object's center alone does not establish that its triangles avoid clipping.
 */
#include <math.h>
#include <string.h>
#include "../engine/gpu_vertex.h"
typedef struct {float lo,hi;} MPStereoInterval;
typedef struct {unsigned used,valid;float lo[11][4],hi[11][4];} MPStereoBounds;
/* PICA float24 has 16 fraction bits. This deliberately larger per-operation
 * allowance covers input conversion and intermediate arithmetic, as well as
 * host float32 endpoint rounding. Small/subnormal results are padded by a
 * floor, and inputs outside the audited numeric domain are rejected. */
#define MP_STEREO_INTERVAL_EPS (1.f/16384.f)
#define MP_STEREO_INTERVAL_FLOOR 1.e-12f
/* Inputs to interval arithmetic are finite and bounded before use. Their
 * products fit in float32 (at most roughly 1e24), and each transform result is
 * checked before the next transform. Avoid out-of-line newlib fminf/fmaxf and
 * fpclassify calls for this audited domain. The ordered magnitude comparison
 * also rejects NaN and both infinities without a separate classifier. */
#if defined(MP_STEREO_BOUNDS_REFERENCE) || defined(MP_STEREO_BOUNDS_LIBRARY_MATH)
static inline int mp_stereo_number(float x){return isfinite(x)&&fabsf(x)<=1.e12f;}
static inline float mp_stereo_min(float a,float b){return fminf(a,b);}
static inline float mp_stereo_max(float a,float b){return fmaxf(a,b);}
#else
static inline int mp_stereo_number(float x){return fabsf(x)<=1.e12f;}
static inline float mp_stereo_min(float a,float b){return a<b?a:b;}
static inline float mp_stereo_max(float a,float b){return a>b?a:b;}
#endif
static inline MPStereoInterval mp_stereo_pad(float lo,float hi){
    float error=mp_stereo_max(fabsf(lo),fabsf(hi))*MP_STEREO_INTERVAL_EPS+MP_STEREO_INTERVAL_FLOOR;
    return (MPStereoInterval){lo-error,hi+error};
}
static inline int mp_stereo_interval_valid(MPStereoInterval x){
    return mp_stereo_number(x.lo)&&mp_stereo_number(x.hi)&&x.lo<=x.hi;
}
static inline MPStereoInterval mp_stereo_add(MPStereoInterval a,MPStereoInterval b){
    return mp_stereo_pad(a.lo+b.lo,a.hi+b.hi);
}
static inline MPStereoInterval mp_stereo_mul(MPStereoInterval a,MPStereoInterval b){
    float p=a.lo*b.lo,q=a.lo*b.hi,r=a.hi*b.lo,s=a.hi*b.hi;
    return mp_stereo_pad(mp_stereo_min(mp_stereo_min(p,q),mp_stereo_min(r,s)),mp_stereo_max(mp_stereo_max(p,q),mp_stereo_max(r,s)));
}
static inline MPStereoInterval mp_stereo_dot(const float a[4],const MPStereoInterval b[4]){
#ifdef MP_STEREO_BOUNDS_REFERENCE
    MPStereoInterval result={0,0};
    for(unsigned k=0;k<4;++k)result=mp_stereo_add(result,mp_stereo_mul(mp_stereo_pad(a[k],a[k]),b[k]));
    return result;
#else
    /* The coefficient is a point, not another arbitrary interval. Select its
     * two endpoint products directly and bound rounding for the whole dot.
     * Four additions, four products and coefficient conversion need less than
     * 8*EPS relative to the sum of absolute terms; 16*EPS also covers host
     * endpoint arithmetic. FLOOR covers the coefficient/subnormal allowance.
     * Input intervals already include their earlier transform uncertainty. */
    float lo=0,hi=0,magnitude=0,input_magnitude=0;
    for(unsigned k=0;k<4;++k){
        float p=a[k]*b[k].lo,q=a[k]*b[k].hi;
        if(a[k]<0){float swap=p;p=q;q=swap;}
        lo+=p;hi+=q;
        magnitude+=mp_stereo_max(fabsf(p),fabsf(q));
        input_magnitude+=mp_stereo_max(fabsf(b[k].lo),fabsf(b[k].hi));
    }
    float error=magnitude*(16*MP_STEREO_INTERVAL_EPS)+(input_magnitude+1)*(16*MP_STEREO_INTERVAL_FLOOR);
    return (MPStereoInterval){lo-error,hi+error};
#endif
}
static inline void mp_stereo_bounds_build(MPStereoBounds*b,const MPGPUVertex*v,unsigned count){
    memset(b,0,sizeof(*b));b->valid=count!=0;
    for(unsigned i=0;i<count;++i){
        float row=v[i].normal[3];unsigned group;
        if(!mp_stereo_number(row)){b->valid=0;return;}
        if(row<0)group=10;
        else{
            if(row>27||(unsigned)row%3||row!=(float)(unsigned)row){b->valid=0;return;}
            group=(unsigned)row/3;
        }
        unsigned bit=1u<<group;
        for(unsigned k=0;k<4;++k){float x=v[i].pos[k];
            if(!mp_stereo_number(x)){b->valid=0;return;}
            if(!(b->used&bit))b->lo[group][k]=b->hi[group][k]=x;
            else{b->lo[group][k]=mp_stereo_min(b->lo[group][k],x);b->hi[group][k]=mp_stereo_max(b->hi[group][k],x);}
        }
        b->used|=bit;
    }
}
static inline int mp_stereo_bounds_inside(const MPStereoBounds*b,const float matrix[30][4],
                                          const float projection[4][4],float scale,float bias,float horizontal_limit){
    if(!b->valid||!b->used||!mp_stereo_number(scale)||!mp_stereo_number(bias))return 0;
    if(!(horizontal_limit>0&&horizontal_limit<=1))return 0;
    for(unsigned k=0;k<16;++k)if(!mp_stereo_number(((const float*)projection)[k]))return 0;
    for(unsigned group=0;group<11;++group){
        if(!(b->used&(1u<<group)))continue;
        MPStereoInterval source[4],world[4],clip[4];
        for(unsigned k=0;k<4;++k)source[k]=mp_stereo_pad(b->lo[group][k],b->hi[group][k]);
        if(group==10)memcpy(clip,source,sizeof(clip));
        else{
            for(unsigned k=0;k<12;++k)if(!mp_stereo_number(((const float*)&matrix[group*3])[k]))return 0;
            for(unsigned k=0;k<3;++k){world[k]=mp_stereo_dot(matrix[group*3+k],source);if(!mp_stereo_interval_valid(world[k]))return 0;}
            world[3]=(MPStereoInterval){1,1};
            for(unsigned k=0;k<4;++k){clip[k]=mp_stereo_dot(projection[k],world);if(!mp_stereo_interval_valid(clip[k]))return 0;}
        }
        /* Additional screen-space margin rejects near-edge uncertainty.
         * No ratio/division is needed: test homogeneous plane distances. */
        float margin=.02f*clip[3].hi+MP_STEREO_INTERVAL_FLOOR;
        if(clip[3].lo<=margin||clip[0].lo<=-clip[3].lo+margin||clip[0].hi>=clip[3].lo-margin||
           clip[2].lo<=-clip[3].lo||clip[2].hi>=0)return 0;
        MPStereoInterval shift=mp_stereo_add(mp_stereo_mul(mp_stereo_pad(scale,scale),clip[3]),mp_stereo_pad(bias,bias));
        for(unsigned eye=0;eye<2;++eye){
            MPStereoInterval delta=eye?(MPStereoInterval){-shift.hi,-shift.lo}:shift;
            MPStereoInterval y=mp_stereo_add(clip[1],delta);
            if(!mp_stereo_interval_valid(y)||y.lo<=-horizontal_limit*clip[3].lo+margin||y.hi>=horizontal_limit*clip[3].lo-margin)return 0;
        }
    }
    return 1;
}
#endif
