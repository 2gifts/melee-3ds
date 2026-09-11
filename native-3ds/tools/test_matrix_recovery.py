"""Check the production recovery walk against clean, independently cached trees."""
import subprocess
from build import ROOT,local_clang

def main():
    # Model HSD's lazy class dispatch/dirty propagation, then execute the
    # actual recovery function. The reference is a complete clean traversal,
    # saved before poisoning; it does not use the recovery walk.
    prefix=r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct HSD_JObj {
    struct HSD_JObj *parent,*child,*next;
    float local[3][4],mtx[3][4];unsigned dirty,builds;
} HSD_JObj;
static void HSD_JObjSetMtxDirty(HSD_JObj*p){
    if(!p||p->dirty)return;
    p->dirty=1;for(HSD_JObj*c=p->child;c;c=c->next)HSD_JObjSetMtxDirty(c);
}
static void HSD_JObjSetupMatrix(HSD_JObj*p){
    if(!p||!p->dirty)return;
    HSD_JObjSetupMatrix(p->parent);++p->builds;
    if(!p->parent)memcpy(p->mtx,p->local,sizeof(p->mtx));
    else for(int i=0;i<3;++i)for(int j=0;j<4;++j){
        float sum=j==3?p->parent->mtx[i][3]:0;
        for(int k=0;k<3;++k)sum+=p->parent->mtx[i][k]*p->local[k][j];
        p->mtx[i][j]=sum;
    }
    p->dirty=0;
}
'''
    suffix=r'''
static uint32_t rng=0x75121234;
static unsigned random_word(void){rng=rng*1664525u+1013904223u;return rng;}
int main(void){
    unsigned parents=0;
    for(unsigned run=0;run<20000;++run){
        HSD_JObj joints[64]={0};float clean[64][3][4];unsigned n=1+random_word()%64;
        for(unsigned i=0;i<n;++i){
            HSD_JObj*p=&joints[i];p->parent=i?&joints[i-1]:0;p->child=i+1<n?&joints[i+1]:0;p->dirty=1;
            float angle=(int)(random_word()%6000)/1000.f-3,scale=.9f+(random_word()%200)/1000.f;
            p->local[0][0]=cosf(angle)*scale;p->local[0][1]=-sinf(angle)*scale;
            p->local[1][0]=sinf(angle)*scale;p->local[1][1]=cosf(angle)*scale;p->local[2][2]=scale;
            for(int k=0;k<3;++k)p->local[k][3]=(int)(random_word()%10000)/1000.f-5;
        }
        HSD_JObjSetupMatrix(&joints[n-1]);
        for(unsigned i=0;i<n;++i){memcpy(clean[i],joints[i].mtx,sizeof(clean[i]));joints[i].builds=0;}
        unsigned first=random_word()%n;
        for(unsigned i=first;i<n;++i)for(int a=0;a<3;++a)for(int b=0;b<4;++b)joints[i].mtx[a][b]=NAN;
        unsigned failures=mp_collision_matrix_failed;
        mp_collision_repair_matrix(&joints[n-1]);
        assert(mp_collision_matrix_failed==failures);
        for(unsigned i=0;i<n;++i){assert(!memcmp(clean[i],joints[i].mtx,sizeof(clean[i])));assert(joints[i].builds==(i>=first));}
        parents+=n-first-1;
    }
    assert(mp_collision_parent_repairs==parents);
    HSD_JObj invalid={0};invalid.local[0][0]=invalid.mtx[0][0]=NAN;
    unsigned failed=mp_collision_matrix_failed;mp_collision_repair_matrix(&invalid);
    assert(mp_collision_matrix_failed==failed+1);
    HSD_JObj too_deep[65]={0};for(unsigned i=0;i<65;++i){too_deep[i].mtx[0][0]=NAN;too_deep[i].parent=i?&too_deep[i-1]:0;}
    mp_collision_repair_matrix(&too_deep[64]);assert(mp_collision_matrix_failed==failed+2);
    for(unsigned i=0;i<65;++i)assert(too_deep[i].builds==0);
    printf("Matrix recovery: 20000 poisoned hierarchies match clean matrices byte-for-byte; %u ancestor rebuilds; invalid source and 65-joint bound handled\n",parents);
}
'''
    production=(ROOT/'port/engine/collision_matrix.c').read_text()
    production=production.replace('#include <sysdolphin/baselib/jobj.h>','')
    source=ROOT/'build/matrix-recovery-test.c';source.write_text(prefix+production+suffix)
    exe=source.with_suffix('.exe')
    subprocess.run([local_clang(),'-O2','-ffp-contract=off',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
