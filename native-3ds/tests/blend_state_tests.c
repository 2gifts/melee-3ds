#include <assert.h>
#include <stdio.h>
#include "../port/3ds/blend_state.h"
static double native_factor(unsigned f,double s,double d,double sa,double da){
    switch(f){case 0:return 0;case 1:return 1;case 2:return s;case 3:return 1-s;
    case 4:return d;case 5:return 1-d;case 6:return sa;case 7:return 1-sa;case 8:return da;case 9:return 1-da;default:assert(0);return 0;}
}
static double gx_factor(unsigned f,double opposite,double sa,double da){
    switch(f){case 0:return 0;case 1:return 1;case 2:return opposite;case 3:return 1-opposite;
    case 4:return sa;case 5:return 1-sa;case 6:return da;case 7:return 1-da;default:assert(0);return 0;}
}
int main(void){
    const double values[]={0,1.0/255,31.0/255,63.0/255,127.0/255,191.0/255,254.0/255,1};
    unsigned cases=0;
    for(unsigned alpha=0;alpha<2;++alpha)for(unsigned src=0;src<8;++src)for(unsigned dst=0;dst<8;++dst){
        MPBlendState b=mp_blend_state(1|(alpha<<16),src,dst);assert(!b.logical&&b.equation==0);
        for(unsigned i=0;i<8;++i)for(unsigned j=0;j<8;++j)for(unsigned k=0;k<8;++k)for(unsigned l=0;l<8;++l){
            double s=values[i],d=values[j],sa=values[k],da=values[l],gda=alpha?da:1;
            assert(native_factor(b.src,s,d,sa,da)==gx_factor(src,d,sa,gda));
            assert(native_factor(b.dst,s,d,sa,da)==gx_factor(dst,s,sa,gda));
            assert(native_factor(b.src_alpha,sa,da,sa,da)==gx_factor(src,gda,sa,gda));
            assert(native_factor(b.dst_alpha,sa,da,sa,da)==gx_factor(dst,sa,sa,gda));++cases;
        }
    }
    MPBlendState b=mp_blend_state(3,0,0);assert(b.equation==2&&b.src==1&&b.dst==1&&b.src_alpha==1&&b.dst_alpha==1);
    b=mp_blend_state(0,7,7);assert(!b.logical&&b.equation==0&&b.src==1&&!b.dst&&b.src_alpha==1&&!b.dst_alpha);
    printf("GX blend factors: %u color/alpha/buffer combinations passed, plus replacement and reverse subtraction\n",cases);
}
