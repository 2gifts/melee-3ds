"""Stress conservative stereo bounds against per-vertex float24-like math.

This arithmetic oracle tests both truncating and nearest mantissa rounding.
It does not replace actual GPU comparisons or claim a hardware timing model.
"""
import hashlib, json, subprocess
from build import ROOT,local_clang


def main():
    out=ROOT/'build/stereo-bounds-host';out.mkdir(exist_ok=True)
    source=out/'test.c'
    source.write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "stereo_bounds.h"
static uint32_t rng=0x8ce78143;
static float randomf(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (float)(rng&65535)/32768.f-1.f;}
static float quant(float x,unsigned nearest){
    uint32_t b;memcpy(&b,&x,4);if(nearest)b+=64;b&=~127u;memcpy(&x,&b,4);return x;
}
static float dot(const float*a,const float*b,unsigned mode){
    float x=0;for(unsigned k=0;k<4;++k)x=quant(x+quant(quant(a[k],mode)*quant(b[k],mode),mode),mode);return x;
}
int main(void){
    unsigned accepted=0,checked=0,rejected=0,invalid=0,dot_checks=0;
    /* Independent endpoint oracle: cancellation, mixed signs and scales.
     * Every box corner is checked with both mantissa rounding models. */
    for(unsigned trial=0;trial<40000;++trial){
        float a[4],ends[4][2];MPStereoInterval b[4];
        for(unsigned k=0;k<4;++k){
            a[k]=ldexpf(randomf(),(int)((trial+k*7)%61)-30);
            float x=ldexpf(randomf(),(int)((trial*3+k*11)%61)-30);
            float y=x+fabsf(x)*fabsf(randomf());
            ends[k][0]=x;ends[k][1]=y;b[k]=mp_stereo_pad(x,y);
        }
        if(trial%3==0){a[1]=-a[0];ends[1][0]=ends[0][0];ends[1][1]=ends[0][1];b[1]=b[0];}
        MPStereoInterval result=mp_stereo_dot(a,b);
        for(unsigned corner=0;corner<16;++corner)for(unsigned mode=0;mode<2;++mode){
            float v[4];for(unsigned k=0;k<4;++k)v[k]=ends[k][(corner>>k)&1];
            float value=dot(a,v,mode);assert(value>=result.lo&&value<=result.hi);++dot_checks;
        }
    }
    rng=0x8ce78143;
    for(unsigned trial=0;trial<50000;++trial){
        float m[30][4]={{0}},p[4][4]={{0}};MPGPUVertex v[12]={0};MPStereoBounds b;
        float z=-80.f-randomf()*40.f;
        if(trial%19==0)z=-.04f;
        if(trial%23==0)z=10.f;
        for(unsigned group=0;group<10;++group){
            for(unsigned k=0;k<3;++k){m[group*3+k][k]=1.f+randomf()*.1f;m[group*3+k][(k+1)%3]=randomf()*.3f;}
            m[group*3][3]=randomf()*15.f;m[group*3+1][3]=randomf()*15.f;m[group*3+2][3]=z;
        }
        p[0][1]=3.8667123f;p[1][0]=-3.1763792f;p[2][2]=-6.103553e-6f;p[2][3]=-.10000061f;p[3][2]=-1;
        for(unsigned i=0;i<12;++i){
            for(unsigned k=0;k<3;++k)v[i].pos[k]=randomf()*7.f;
            v[i].pos[3]=1;v[i].normal[3]=(float)((i%3)*3+(trial%2?0:21));
            if(trial%7==0){v[i].normal[3]=-1;v[i].pos[0]=randomf()*.7f;v[i].pos[1]=randomf()*.7f;v[i].pos[2]=-.5f;v[i].pos[3]=1;}
        }
        float scale=.03f,bias=-scale*80.f,limit=trial&1?.75f:1.f;
        if(trial%7==0)bias=0;
        mp_stereo_bounds_build(&b,v,12);
        if(!mp_stereo_bounds_inside(&b,m,p,scale,bias,limit)){++rejected;continue;}
        ++accepted;
        for(unsigned mode=0;mode<2;++mode)for(unsigned i=0;i<12;++i){
            float c[4],world[4];
            if(v[i].normal[3]<0)for(unsigned k=0;k<4;++k)c[k]=quant(v[i].pos[k],mode);
            else{
                unsigned row=(unsigned)v[i].normal[3];
                for(unsigned k=0;k<3;++k)world[k]=dot(m[row+k],v[i].pos,mode);world[3]=1;
                for(unsigned k=0;k<4;++k)c[k]=dot(p[k],world,mode);
            }
            for(int eye=-1;eye<=1;eye+=2){
                float y=quant(quant(quant(eye*scale,mode)*c[3],mode)+c[1],mode);y=quant(y+quant(eye*bias,mode),mode);
                assert(c[3]>0&&fabsf(c[0])<c[3]&&c[2]<0&&c[2]>-c[3]&&fabsf(y)<limit*c[3]);++checked;
            }
        }
    }
    float m[30][4]={{0}},p[4][4]={{0}};MPGPUVertex v={0};MPStereoBounds b;
    mp_stereo_bounds_build(&b,&v,0);assert(!mp_stereo_bounds_inside(&b,m,p,0,0,1));++invalid;
    float bad[]={NAN,INFINITY,-INFINITY,1.e20f};
    for(unsigned i=0;i<4;++i){v.pos[0]=bad[i];mp_stereo_bounds_build(&b,&v,1);assert(!b.valid);++invalid;}
    v.pos[0]=0;float rows[]={.5f,1.f,28.f,30.f,NAN,INFINITY};
    for(unsigned i=0;i<6;++i){v.normal[3]=rows[i];mp_stereo_bounds_build(&b,&v,1);assert(!b.valid);++invalid;}
    assert(accepted>10000&&rejected>1000&&checked>400000);
    printf("{\"trials\":50000,\"accepted\":%u,\"rejected\":%u,\"quantized_eye_vertex_checks\":%u,\"invalid_inputs_rejected\":%u,\"dot_corner_checks\":%u}\n",accepted,rejected,checked,invalid,dot_checks);
}
''')
    exe=out/'test.exe'
    subprocess.run([str(local_clang()),'-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'port/3ds'),str(source),'-o',str(exe)],check=True)
    result=subprocess.check_output([str(exe)],text=True);data=json.loads(result)
    data['bounds_source_sha256']=hashlib.sha256((ROOT/'port/3ds/stereo_bounds.h').read_bytes()).hexdigest()
    data['test_source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
    (out/'result.json').write_text(json.dumps(data,indent=2)+'\n');print(result,end='')


if __name__=='__main__':main()
