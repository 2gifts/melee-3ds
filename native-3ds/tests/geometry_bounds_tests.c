#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../port/engine/geometry_bounds.h"
#include "../port/engine/stereo_config.h"
static unsigned random_state=19;
static float random_float(void){random_state=random_state*1664525u+1013904223u;return (int)(random_state>>8)/8388608.f-1;}
int main(void){
    MPGPUVertex v[2]={0};MPGeometryBounds b;MPGPUUniforms u={0};
    for(int i=0;i<3;++i)u.value[i][i]=1;
    for(int i=0;i<4;++i)u.value[MP_GPU_PROJECTION+i][i]=1;
    for(int i=0;i<2;++i){v[i].pos[0]=i?1:-1;v[i].pos[1]=i?1:-1;v[i].pos[2]=i?1:-1;v[i].pos[3]=1;}
    mp_bounds_build(&b,v,2);assert(b.row==0&&!mp_bounds_outside(&b,&u));
    u.value[0][3]=5;assert(mp_bounds_outside(&b,&u));u.value[0][3]=0;
    v[1].normal[3]=3;mp_bounds_build(&b,v,2);assert(b.row==-1&&!mp_bounds_outside(&b,&u));v[1].normal[3]=0;
    v[1].pos[0]=NAN;mp_bounds_build(&b,v,2);assert(b.row==-1);v[1].pos[0]=1;
    unsigned rejected=0,stereo_rejected=0;
    for(unsigned test=0;test<100000;++test){
        for(unsigned i=0;i<3;++i){float center=random_float()*500,extent=fabsf(random_float()*50);
            v[0].pos[i]=center-extent;v[1].pos[i]=center+extent;}
        mp_bounds_build(&b,v,2);
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)u.value[i][j]=random_float()*3;
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)u.value[MP_GPU_PROJECTION+i][j]=random_float()*2;
        if(mp_bounds_outside(&b,&u)){
            ++rejected;unsigned outside=15;
            for(unsigned corner=0;corner<8;++corner){
                double x[4],eye[4]={0,0,0,1},clip[4]={0};
                for(unsigned k=0;k<3;++k)x[k]=(corner>>k)&1?b.hi[k]:b.lo[k];x[3]=1;
                for(unsigned i=0;i<3;++i)for(unsigned k=0;k<4;++k)eye[i]+=(double)u.value[i][k]*x[k];
                for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)clip[i]+=(double)u.value[MP_GPU_PROJECTION+i][k]*eye[k];
                unsigned mask=0;for(unsigned p=0;p<4;++p)if(clip[3]+((p&1)?-1:1)*clip[p/2]<0)mask|=1u<<p;
                outside&=mask;
            }
            assert(outside); /* An independent double-precision corner oracle. */
        }
        float maximum_strength=1000*MP_STEREO_PIXELS_PER_SLIDER/320;
        float strength=test%4?fabsf(random_float())*maximum_strength*1.25f:maximum_strength;
        float convergence=fabsf(random_float())*300;
        if(mp_bounds_outside_stereo(&b,&u,strength,convergence)){
            ++stereo_rejected;unsigned outside=15;
            for(unsigned view=0;view<2;++view)for(unsigned corner=0;corner<8;++corner){
                double x[4],eye[4]={0,0,0,1},clip[4]={0};
                for(unsigned k=0;k<3;++k)x[k]=(corner>>k)&1?b.hi[k]:b.lo[k];x[3]=1;
                for(unsigned i=0;i<3;++i)for(unsigned k=0;k<4;++k)eye[i]+=(double)u.value[i][k]*x[k];
                for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)clip[i]+=(double)u.value[MP_GPU_PROJECTION+i][k]*eye[k];
                clip[1]+=(view?-1:1)*(double)strength*(clip[3]-convergence);
                unsigned mask=0;for(unsigned p=0;p<4;++p)if(clip[3]+((p&1)?-1:1)*clip[p/2]<0)mask|=1u<<p;
                outside&=mask;
            }
            assert(outside);
        }
    }
    assert(rejected>1000&&stereo_rejected>1000);printf("Conservative bounds: 100000 mono/stereo transforms, %u/%u rejected; all corners in both eyes outside a shared plane\n",rejected,stereo_rejected);
}
