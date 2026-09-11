"""Regression for the real capsule solver, using point/sphere geometry oracles."""
from pathlib import Path
import subprocess
from build import ROOT,local_clang
from engine_overlays import adapt

def function(source,name):
    import re
    m=re.search(r'^(?:bool|float) '+name+r'\([^;]+?\)\s*\{',source,re.M)
    assert m,name
    end=m.end();depth=1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[m.start():end]

def main():
    source=ROOT/'upstream/melee/src/melee/lb/lbcollision.c'
    text=adapt(source).read_text()
    prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef struct {float x,y,z;} Vec3;
typedef float Mtx[3][4];typedef float (*MtxPtr)[4];
#define PAD_STACK(x)
#define OSReport(...) ((void)0)
unsigned mp_collision_invalid_count;float mp_collision_invalid_sample[32];
static bool approximatelyZero(float x){return x<.00001f && x>-.00001f;}
static void HSD_MtxInverse(Mtx in,Mtx out){
    for(int i=0;i<3;++i)for(int j=0;j<4;++j)assert(in[i][j]==(float)(i==j));
    memcpy(out,in,sizeof(Mtx));
}
static void PSMTXMultVec(Mtx m,Vec3* in,Vec3*out){
    Vec3 a=*in;
    out->x=m[0][0]*a.x+m[0][1]*a.y+m[0][2]*a.z+m[0][3];
    out->y=m[1][0]*a.x+m[1][1]*a.y+m[1][2]*a.z+m[1][3];
    out->z=m[2][0]*a.x+m[2][1]*a.y+m[2][2]*a.z+m[2][3];
}
'''
    suffix=r'''
int main(void){
    Mtx identity={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    Vec3 h={-231.5862f,-73.0927f,1.8681f},bad={NAN,NAN,NAN},a,b,contact;float overlap;
    assert(!lbColl_80006E58(&h,&h,&bad,&bad,&a,&b,identity,&contact,&overlap,8,2.4f,3));
    assert(mp_collision_invalid_count==1);
    uint32_t rng=0x1234;unsigned hits=0;
    for(unsigned i=0;i<50000;++i){
        float v[8];for(int j=0;j<8;++j){rng=rng*1664525+1013904223;v[j]=(int32_t)(rng>>8)%20000/1000.f-10;}
        Vec3 p={v[0],v[1],v[2]},q={v[3],v[4],v[5]};float r1=fabsf(v[6]),r2=fabsf(v[7]);
        double dx=(double)p.x-q.x,dy=(double)p.y-q.y,dz=(double)p.z-q.z;
        double distance=sqrt(dx*dx+dy*dy+dz*dz),radius=r1+r2;
        bool actual=lbColl_80006E58(&p,&p,&q,&q,&a,&b,identity,&contact,&overlap,r1,r2,3);
        if(fabs(distance-radius)>1e-4)assert(actual==(distance<=radius));
        if(actual){++hits;assert(isfinite(contact.x)&&isfinite(contact.y)&&isfinite(contact.z)&&isfinite(overlap));}
    }
    Vec3 same={0,0,0};assert(lbColl_80006E58(&same,&same,&same,&same,&a,&b,identity,&contact,&overlap,2,3,3));
    assert(overlap==5 && contact.x==0 && contact.y==0 && contact.z==0);
    printf("Capsule contacts: invalid remote hazard rejected; 50000 sphere comparisons, %u hits; coincident axes finite\n",hits);
}
'''
    out=ROOT/'build/collision-contacts-tests.c'
    out.write_text(prefix+function(text,'lbColl_80005EBC')+'\n'+function(text,'lbColl_80006E58')+suffix)
    exe=out.with_suffix('.exe')
    subprocess.run([local_clang(),'-O2','-ffp-contract=off','-Wall','-Wextra',str(out),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
