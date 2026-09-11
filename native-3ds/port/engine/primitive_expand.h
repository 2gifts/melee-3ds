#ifndef MP_PRIMITIVE_EXPAND_H
#define MP_PRIMITIVE_EXPAND_H
#include "gpu_vertex.h"
#include <math.h>
/* Width registers use sixths of an EFB pixel. Work in the full 640x480
 * clip space after the GX viewport, before the native screen rotation. */
static inline float mp_tex_offset(unsigned mode){
    const float values[]={0,1.f/16,1.f/8,1.f/4,1.f/2,1};
    return mode<6?values[mode]:0;
}
static inline int mp_expand_point(const MPGPUVertex*v,unsigned width,float tex,MPGPUVertex q[4]){
    if(!width||v->pos[3]<=0||v->pos[2]<-v->pos[3]||v->pos[2]>0)return 0;
    float dx=width*v->pos[3]/3840.f,dy=width*v->pos[3]/2880.f;
    for(unsigned i=0;i<4;++i){
        q[i]=*v;q[i].pos[0]+=(i&1)?dx:-dx;q[i].pos[1]+=(i&2)?-dy:dy;
        q[i].uv[0]+=(i&1)?tex:0;q[i].uv[1]+=(i&2)?tex:0;
    }
    return 1;
}
static inline void mp_clip_vertex(MPGPUVertex*out,const MPGPUVertex*a,const MPGPUVertex*b,float t){
    for(unsigned i=0;i<4;++i){out->pos[i]=a->pos[i]+(b->pos[i]-a->pos[i])*t;out->color[i]=a->color[i]+(b->color[i]-a->color[i])*t;}
    for(unsigned i=0;i<2;++i)out->uv[i]=a->uv[i]+(b->uv[i]-a->uv[i])*t;
    out->normal[0]=out->normal[1]=out->normal[2]=0;out->normal[3]=-1;
}
static inline int mp_expand_line(const MPGPUVertex*start,const MPGPUVertex*end,unsigned width,float tex,MPGPUVertex q[4]){
    if(!width)return 0;
    MPGPUVertex a=*start,b=*end;
    /* Clip depth before dividing by w to choose the expansion axis. */
    for(unsigned plane=0;plane<3;++plane){
        float da=plane==0?a.pos[3]-1.e-6f:plane==1?a.pos[2]+a.pos[3]:-a.pos[2];
        float db=plane==0?b.pos[3]-1.e-6f:plane==1?b.pos[2]+b.pos[3]:-b.pos[2];
        if(da<0&&db<0)return 0;
        if(da<0||db<0){MPGPUVertex intersection;mp_clip_vertex(&intersection,&a,&b,da/(da-db));if(da<0)a=intersection;else b=intersection;}
    }
    float x=fabsf(b.pos[0]/b.pos[3]-a.pos[0]/a.pos[3])*640;
    float y=fabsf(b.pos[1]/b.pos[3]-a.pos[1]/a.pos[3])*480;
    /* GX caps remain horizontal/vertical, selected by the major axis. */
    float dx=y>x?width/3840.f:0,dy=y>x?0:-(float)width/2880.f;
    for(unsigned i=0;i<4;++i){
        q[i]=i<2?a:b;float side=(i&1)?1:-1;
        q[i].pos[0]+=side*dx*q[i].pos[3];q[i].pos[1]+=side*dy*q[i].pos[3];
        q[i].uv[0]+=(i&1)?tex:0;
    }
    return 1;
}
#endif
