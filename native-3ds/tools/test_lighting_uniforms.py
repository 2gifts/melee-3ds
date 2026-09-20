"""Exhaustive draw-enum predicates versus the preceding shader's float tests."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,default=ROOT/'build/update17-host-current')
    OUT=ap.parse_args().output
    OUT.mkdir(parents=True,exist_ok=True)
    code=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../engine/gpu_vertex.h"
typedef uint32_t u32;
static void mp_native_panic(const char*s){fprintf(stderr,"%s\n",s);abort();}
static u32 read32(const void*p){u32 x;memcpy(&x,p,4);return __builtin_bswap32(x);}
#include "lighting_uniforms.h"
static void be(float*p,float f){u32 v;memcpy(&v,&f,4);v=__builtin_bswap32(v);memcpy(p,&v,4);}
int main(void){
    unsigned count=0,seen[16][2]={{0}};
    assert(sizeof(lighting_boolean_names)/sizeof(*lighting_boolean_names)==16);
    for(unsigned enabled=0;enabled<4;++enabled)
    for(unsigned a0=0;a0<3;++a0)for(unsigned a1=0;a1<3;++a1)
    for(unsigned tags=0;tags<81;++tags){
        MPGPUUniforms u={0};float enable[2]={(enabled&1)!=0,(enabled&2)!=0};
        float attenuation[2]={(float)a0,(float)a1};unsigned expected=0,t=tags;
        for(unsigned ch=0;ch<2;++ch){
            be(&u.value[91+ch][0],enable[ch]);be(&u.value[91+ch][3],attenuation[ch]);
            if(0.f<enable[ch])expected|=1u<<(14+ch);
        }
        for(unsigned i=0;i<4;++i){
            float w=(float)(t%3);t/=3;be(&u.value[72+i][3],w);
            if(0.f<w)expected|=1u<<(2+i);
            if(1.f<w)expected|=1u<<(6+i);
            if(2.f>attenuation[1.f<w])expected|=1u<<(10+i);
        }
        unsigned actual=lighting_uniform_mask(&u);assert(actual==expected);
        for(unsigned bit=2;bit<16;++bit)++seen[bit][(actual>>bit)&1];
        ++count;
    }
    for(unsigned bit=2;bit<16;++bit)assert(seen[bit][0]&&seen[bit][1]);
    printf("PASS: %u enum combinations, all 14 lighting bits true and false\n",count);
}
'''
    source=OUT/'test.c';source.write_text(code)
    exe=OUT/'test.exe'
    subprocess.run([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'),
                    '-O2','-Wall','-Wextra','-Werror','-DMP_SMOKE_TEST',
                    '-I'+str(ROOT/'port/3ds'),str(source),'-o',str(exe)],check=True)
    result=subprocess.check_output([str(exe)],text=True)
    (OUT/'result.txt').write_text(result)
    shaders={}
    for name in ('vertex','vertex-dual','vertex-stereo','vertex-dual-stereo'):
        reference=(ROOT/f'build/update17-baseline/{name}.v.pica').read_text()
        generated=(ROOT/f'build/game/shader-reference/{name}.v.pica').read_text()
        assert reference==generated, 'Reference changed: '+name
        candidate=(ROOT/f'port/3ds/{name}.v.pica').read_text()
        # The scheduling change removes a redundant channel-enable sum and
        # moves four distance multiplies to their spotlight-only uses.
        # All other position/normal/color/UV arithmetic stays in order.
        def arithmetic(text):
            return [line.strip() for line in text.splitlines()
                    if line.strip().split(' ')[0] in ('mova','dp3','dp4','mul','add','mad','max','min','rsq','rcp','sge')]
        before=arithmetic(reference);after=arithmetic(candidate)
        assert before.count('add r0.x, r0.x, r3.x')==1
        assert after.count('add r0.x, r0.x, r3.x')==0
        before.remove('add r0.x, r0.x, r3.x')
        distance='mul r3.z, r3.x, r3.y'
        assert before.count(distance)==after.count(distance)==4
        assert [x for x in after if x!=distance]==[x for x in before if x!=distance],name
        assert candidate.count('                    '+distance+'\n                    mov r12.y, r3.z')==4
        assert candidate.count('        ifu light')==16
        shaders[name]=dict(reference_sha256=hashlib.sha256(reference.encode()).hexdigest(),
                           candidate_sha256=hashlib.sha256(candidate.encode()).hexdigest(),
                           ordered_arithmetic_except_documented_schedule_identical=True)
    (OUT/'shaders.json').write_text(json.dumps(shaders,indent=2)+'\n')
    print(result,end='');print('PASS: all other ordered arithmetic retained around the bounded lighting schedule changes')
if __name__=='__main__':main()
