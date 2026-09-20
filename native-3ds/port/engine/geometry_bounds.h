#ifndef MP_GEOMETRY_BOUNDS_H
#define MP_GEOMETRY_BOUNDS_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "gpu_vertex.h"
#ifndef MP_BOUNDS_KEY_COMPARE
#define MP_BOUNDS_KEY_COMPARE memcmp
#endif
typedef struct {float lo[3],hi[3];int row;} MPGeometryBounds;
static void mp_bounds_build(MPGeometryBounds*b,const MPGPUVertex*v,unsigned n){
    b->row=-1;if(!n)return;
    float row=v[0].normal[3];
    if(!(row>=0&&row<=27)||row!=(int)row||((int)row%3))return;
    for(unsigned k=0;k<3;++k)b->lo[k]=b->hi[k]=v[0].pos[k];
    for(unsigned i=0;i<n;++i){
        if(v[i].normal[3]!=row||v[i].pos[3]!=1)return;
        for(unsigned k=0;k<3;++k){float x=v[i].pos[k];if(!isfinite(x))return;
            if(x<b->lo[k])b->lo[k]=x;if(x>b->hi[k])b->hi[k]=x;}
    }
    b->row=row;
}
/* Test four homogeneous clip half-spaces against the entire local box.
 * No perspective division, near-plane guesses, or per-vertex work on hits.
 * Mixed-matrix skinning and point sprites deliberately keep the normal path.
 * The margin covers float24 rasterization and near-boundary roundoff. */
static int mp_bounds_outside_stereo(const MPGeometryBounds*b,const MPGPUUniforms*u,float strength,float convergence){
    if(b->row<0)return 0;
    for(unsigned plane=0;plane<4;++plane){
        unsigned axis=plane/2;float sign=(plane&1)?-1.f:1.f;
        unsigned views=axis==1&&strength>0?2:1,excluded=0;
        for(unsigned eye=0;eye<views;++eye){
        float p[4],local[4];
        for(unsigned k=0;k<4;++k)p[k]=u->value[MP_GPU_PROJECTION+3][k]+sign*u->value[MP_GPU_PROJECTION+axis][k];
        if(views==2){float shift=(eye?-1:1)*sign*strength;
            for(unsigned k=0;k<4;++k)p[k]+=shift*u->value[MP_GPU_PROJECTION+3][k];p[3]-=shift*convergence;}
        for(unsigned k=0;k<4;++k){local[k]=k==3?p[3]:0;
            for(unsigned j=0;j<3;++j)local[k]+=p[j]*u->value[b->row+j][k];}
        float maximum=local[3],magnitude=fabsf(local[3])+1;
        for(unsigned k=0;k<3;++k){float term=local[k]*(local[k]>=0?b->hi[k]:b->lo[k]);maximum+=term;magnitude+=fabsf(term);}
        if(isfinite(maximum)&&isfinite(magnitude)&&maximum < -.002f*magnitude)++excluded;
        }
        if(excluded==views)return 1;
    }
    return 0;
}
static int mp_bounds_outside(const MPGeometryBounds*b,const MPGPUUniforms*u){return mp_bounds_outside_stereo(b,u,0,0);}
/* Plane transforms depend on the camera and matrix, not on the mesh bounds.
 * Exact key comparison allows adjacent chunks to share the work. A collision
 * only replaces an entry; it can never reuse a different transform. */
typedef struct {
    float key[26],local[4][2][4];
    unsigned valid,views[4];
} MPGeometryPlanes;
typedef struct {
    MPGeometryPlanes entry[32];
    unsigned hits,misses;
} MPGeometryPlaneCache;
static int mp_bounds_outside_cached(MPGeometryPlaneCache*cache,const MPGeometryBounds*b,const MPGPUUniforms*u,float strength,float convergence){
    if(b->row<0)return 0;
    float key[26];uint32_t hash[4];
    __builtin_memcpy(key,u->value[b->row],48);
    __builtin_memcpy(key+12,u->value[MP_GPU_PROJECTION],32);
    __builtin_memcpy(key+20,u->value[MP_GPU_PROJECTION+3],16);
    key[24]=strength;key[25]=convergence;
    __builtin_memcpy(hash,key+8,16);
    unsigned slot=(hash[0]^(hash[1]>>7)^(hash[2]>>13)^(hash[3]>>19))&31;
    MPGeometryPlanes*c=&cache->entry[slot];
    if(c->valid&&!MP_BOUNDS_KEY_COMPARE(c->key,key,sizeof(key)))++cache->hits;
    else {
        ++cache->misses;__builtin_memcpy(c->key,key,sizeof(key));c->valid=1;
        for(unsigned plane=0;plane<4;++plane){
            unsigned axis=plane/2;float sign=(plane&1)?-1.f:1.f;
            unsigned views=axis==1&&strength>0?2:1;c->views[plane]=views;
            for(unsigned eye=0;eye<views;++eye){
                float p[4],*local=c->local[plane][eye];
                for(unsigned k=0;k<4;++k)p[k]=u->value[MP_GPU_PROJECTION+3][k]+sign*u->value[MP_GPU_PROJECTION+axis][k];
                if(views==2){float shift=(eye?-1:1)*sign*strength;
                    for(unsigned k=0;k<4;++k)p[k]+=shift*u->value[MP_GPU_PROJECTION+3][k];p[3]-=shift*convergence;}
                for(unsigned k=0;k<4;++k){local[k]=k==3?p[3]:0;
                    for(unsigned j=0;j<3;++j)local[k]+=p[j]*u->value[b->row+j][k];}
            }
        }
    }
    for(unsigned plane=0;plane<4;++plane){
        unsigned excluded=0,views=c->views[plane];
        for(unsigned eye=0;eye<views;++eye){
            const float*local=c->local[plane][eye];float maximum=local[3],magnitude=fabsf(local[3])+1;
            for(unsigned k=0;k<3;++k){float term=local[k]*(local[k]>=0?b->hi[k]:b->lo[k]);maximum+=term;magnitude+=fabsf(term);}
            if(isfinite(maximum)&&isfinite(magnitude)&&maximum<-.002f*magnitude)++excluded;
        }
        if(excluded==views)return 1;
    }
    return 0;
}

#endif
