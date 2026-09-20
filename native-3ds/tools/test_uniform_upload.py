"""Check production upload state against shader reads across changing draw paths.

Shortcut register requirements are read from the generated PICA programs, not
copied from the upload selector. Full-program draws compare every row the old
uploader updates. Float bits and fixed attributes must remain exact, including
when a previously skipped value becomes live again.
"""
import hashlib
import re
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/update16-uniform-upload-host'

def requirements(text,label):
    declarations=text[text.index('.fvec '):text.index('.constf')]
    uniforms={};offset=0
    for name,count in re.findall(r'(\w+)(?:\[(\d+)\])?',declarations.replace('.fvec ','')):
        uniforms[name]=offset;offset+=int(count or 1)
    assert offset==95
    body=text.split(label+':\n')[1].split('    end\n')[0]
    rows=set()
    for name,index in re.findall(r'\b('+ '|'.join(uniforms)+r')(?:\[([^\]]+)\])?',body):
        if 'a0' in index:
            assert name=='model' and index in ('a0.x','a0.x+1','a0.x+2')
        else:rows.add(uniforms[name]+int(index or 0))
    return sorted(rows)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'port/3ds/vertex.v.pica').read_text()
    flat= requirements(source,'flat_vertex');unlit=requirements(source,'unlit_vertex')
    for name in ('vertex-dual','vertex-stereo','vertex-dual-stereo'):
        text=(ROOT/f'port/3ds/{name}.v.pica').read_text()
        assert requirements(text,'flat_vertex')==flat
        assert requirements(text,'unlit_vertex')==unlit
    program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../engine/gpu_vertex.h"
typedef uint32_t u32;typedef uint8_t u8;
#define GPU_VERTEX_SHADER 0
static u32 uniform_cache[MP_GPU_UNIFORMS][4],material0_cache[4];
static u8 uniform_valid[MP_GPU_UNIFORMS];static int material0_valid;
static unsigned palette_rows_sent,palette_rows_skipped;
static u32 installed[MP_GPU_UNIFORMS][4],fixed[4];
static u32 read32(const void*p){u32 x;memcpy(&x,p,4);return __builtin_bswap32(x);}
static void C3D_FVUnifSet(unsigned stage,unsigned i,float x,float y,float z,float w){
    assert(stage==0&&i<95);float row[]={x,y,z,w};memcpy(installed[i],row,16);
}
static void C3D_FixedAttribSet(unsigned i,float x,float y,float z,float w){
    assert(i==4);float row[]={x,y,z,w};memcpy(fixed,row,16);
}
#include "uniform_upload.h"
static u32 rng=0x156ef704;
static u32 next(void){rng=rng*1664525u+1013904223u;return rng;}
static void set(u32*p,u32 x){*p=__builtin_bswap32(x);}
static unsigned comparisons;
static void check(unsigned i,const MPGPUUniforms*g){
    for(unsigned j=0;j<4;++j)assert(installed[i][j]==read32(&g->value[i][j]));
    ++comparisons;
}
int main(void){
    const unsigned flat[]={FLAT_ROWS},unlit[]={UNLIT_ROWS};
    unsigned transitions[3][3]={{0}},last=0;
    for(unsigned step=0;step<30000;++step){
        MPGPUUniforms g;u32*words=(void*)&g;
        for(unsigned i=0;i<sizeof(g)/4;++i)set(words+i,0x3e000000|(next()&0x7fffff));
        unsigned route=(next()>>16)%3,rows=((next()>>16)%10+1)*3;
        unsigned mask=0;
        for(unsigned i=0;i<rows;i+=3)if((next()>>16)&1)mask|=7u<<i;
        set(&g.matrix_rows,rows);set(&g.constant_color,route==0);
        for(unsigned channel=0;channel<2;++channel){
            unsigned enabled=route==2?((next()>>16)&1):0;
            set((u32*)&g.value[91+channel][0],enabled?0x3f800000:0);
        }
        for(unsigned light=0;light<4;++light){
            unsigned active=(next()>>16)%3;
            set((u32*)&g.value[72+light][3],active==2?0x40000000:active?0x3f800000:0);
        }
        /* Periodic reference draws share the exact cache, exercising A/B
         * switches and values that a shortcut had previously left untouched. */
        uniform_scope_disable=step%7==0;
        upload_gpu_uniforms(&g,mask,route==0?1:route==2?2:0);
        for(unsigned row=0;row<rows;++row)if(mask&(1u<<row))check(row,&g);
        if(route<2){
            const unsigned*required=route?unlit:flat;
            unsigned n=route?sizeof(unlit)/sizeof(*unlit):sizeof(flat)/sizeof(*flat);
            for(unsigned i=0;i<n;++i)check(required[i],&g);
        }else{
            /* The old uploader is the full shader's state oracle, and does
             * not share the candidate's sparse scheduling logic. */
            u32 actual[95][4];memcpy(actual,installed,sizeof(actual));
            upload_uniform_reference(&g,mask);
            assert(!memcmp(actual,installed,sizeof(actual)));
            ++comparisons;
        }
        if(route!=0)for(unsigned j=0;j<4;++j)assert(fixed[j]==read32(&g.material0[j]));
        ++transitions[last][route];last=route;
    }
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)assert(transitions[i][j]>1000);
    assert(palette_rows_sent>100000&&palette_rows_skipped>100000);
    printf("PASS 30000 draws, %u exact shader-state checks, all 9 path transitions >1000\n",comparisons);
    return 0;
}
'''.replace('FLAT_ROWS',','.join(map(str,flat))).replace('UNLIT_ROWS',','.join(map(str,unlit)))
    (OUT/'test.c').write_text(program)
    cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
    exe=OUT/'test.exe'
    subprocess.run([str(cc),'-O2','-Wall','-Wextra','-Werror','-DMP_SMOKE_TEST',
                    '-I'+str(ROOT/'port/3ds'),str(OUT/'test.c'),'-o',str(exe)],check=True)
    result=subprocess.check_output([str(exe)],text=True)
    (OUT/'result.txt').write_text(result+f'shader_sha256={hashlib.sha256(source.encode()).hexdigest()}\nflat={flat}\nunlit={unlit}\n')
    print(result,end='')

if __name__=='__main__':main()
