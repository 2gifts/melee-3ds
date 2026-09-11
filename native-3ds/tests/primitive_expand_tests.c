#include <assert.h>
#include <stdio.h>
#include "../port/engine/primitive_expand.h"
static void near(float a,float b){assert(fabsf(a-b)<.0005f);}
int main(void){
    MPGPUVertex a={.pos={0,0,-.5f,1},.color={1,.25f,.5f,1},.uv={.125f,.25f},.normal={0,0,0,-1}},b=a,q[4];
    for(unsigned width=1;width<=255;++width)for(unsigned depth=1;depth<=1000;depth*=10){
        a.pos[3]=depth;a.pos[2]=-(float)depth*.5f;
        assert(mp_expand_point(&a,width,.0625f,q));
        near((q[1].pos[0]-q[0].pos[0])*320/depth,width/6.f);
        near((q[0].pos[1]-q[2].pos[1])*240/depth,width/6.f);
        near(q[3].uv[0],.1875f);near(q[3].uv[1],.3125f);
        for(unsigned k=0;k<4;++k)near(q[2].color[k],a.color[k]);
    }
    a.pos[3]=1;a.pos[2]=-.5f;b=a;b.pos[0]=.5f;
    assert(mp_expand_line(&a,&b,12,.125f,q));
    near(q[0].pos[0],0);near(q[2].pos[0],.5f);
    near((q[0].pos[1]-q[1].pos[1])*240,2);near(q[1].uv[0],a.uv[0]+.125f);
    b.pos[0]=0;b.pos[1]=.5f;
    assert(mp_expand_line(&a,&b,18,0,q));near((q[1].pos[0]-q[0].pos[0])*320,3);
    near(q[0].pos[1],0);near(q[2].pos[1],.5f);
    /* Near-plane crossing interpolates color/UV instead of dividing behind
       the camera. A wholly clipped segment emits no geometry. */
    a.pos[2]=-2;a.color[0]=0;b.color[0]=1;b.pos[2]=0;
    assert(mp_expand_line(&a,&b,6,0,q));near(q[0].pos[2],-1);near(q[0].color[0],.5f);
    b.pos[2]=-3;assert(!mp_expand_line(&a,&b,6,0,q));
    assert(!mp_expand_point(&a,6,0,q));assert(!mp_expand_line(&a,&b,0,0,q));
    near(mp_tex_offset(0),0);near(mp_tex_offset(1),.0625f);near(mp_tex_offset(5),1);
    puts("GX point/line width, perspective, texture offsets and depth clipping passed");
}
