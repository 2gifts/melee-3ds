/* Independent translation unit retains the original, slower interval path. */
#define MP_STEREO_BOUNDS_REFERENCE
#include "../port/3ds/stereo_bounds.h"
int mp_stereo_bounds_reference(const MPStereoBounds*b,const float m[30][4],
                               const float p[4][4],float scale,float bias,float limit){
    return mp_stereo_bounds_inside(b,m,p,scale,bias,limit);
}
