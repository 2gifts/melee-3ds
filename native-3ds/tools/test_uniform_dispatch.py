"""Compare dispatch with pinned Citro3D, including private shader-constant state."""
import hashlib
import re
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/update16-uniform-host'
OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'references/citro3d/source/uniforms.c').read_text()
source=re.sub(r'^#include .*$', '',source,flags=re.M)
names=['C3D_FVUnif','C3D_IVUnif','C3D_BoolUnifs','C3D_FVUnifDirty','C3D_IVUnifDirty',
       'C3D_BoolUnifsDirty','C3Di_ShaderFVecData','C3Di_FVUnifEverDirty','C3Di_IVUnifEverDirty',
       'C3D_UpdateUniforms','C3Di_DirtyUniforms','C3Di_LoadShaderUniforms','C3Di_ClearShaderUniforms']
reference=re.sub(r'\b('+ '|'.join(names)+r')\b',lambda m:'ref_'+m[0],source)
candidate=re.sub(r'\b(C3D_UpdateUniforms|C3Di_DirtyUniforms|C3Di_LoadShaderUniforms|C3Di_ClearShaderUniforms)\b',lambda m:'__real_'+m[0],source)
stub='''#ifndef UNIFORM_STUB_H
#define UNIFORM_STUB_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;typedef uint16_t u16;
typedef enum {GPU_VERTEX_SHADER,GPU_GEOMETRY_SHADER} GPU_SHADER_TYPE;
typedef struct {float values[4];} C3D_FVec;
typedef u32 C3D_IVec;
typedef struct {u32 id,value[3];} float24Uniform_s;
typedef struct {GPU_SHADER_TYPE type;} dvle_s;
typedef struct {dvle_s*dvle;u16 boolUniformMask,boolUniforms;u32 intUniformMask,intUniforms[4];int numFloat24Uniforms;float24Uniform_s*float24Uniforms;} shaderInstance_s;
#define BIT(x) (1u<<(x))
#define C3D_FVUNIF_COUNT 96
#define C3D_IVUNIF_COUNT 4
#define GPUREG_GSH_BOOLUNIFORM 0x280
#define GPUREG_VSH_BOOLUNIFORM 0x2b0
#define GPUREG_VSH_FLOATUNIFORM_CONFIG 0x2c0
#define GPUREG_VSH_FLOATUNIFORM_DATA 0x2c1
#define GPUREG_VSH_INTUNIFORM_I0 0x2b1
extern C3D_FVec C3D_FVUnif[2][96];extern C3D_IVec C3D_IVUnif[2][4];extern u16 C3D_BoolUnifs[2];
extern bool C3D_FVUnifDirty[2][96],C3D_IVUnifDirty[2][4],C3D_BoolUnifsDirty[2];
void GPUCMD_AddWrite(u32,u32);void GPUCMD_AddWrites(u32,const u32*,u32);void GPUCMD_AddIncrementalWrites(u32,const u32*,u32);
extern unsigned gpuCmdBufOffset;
#endif
'''
(OUT/'citro3d.h').write_text(stub)
(OUT/'reference.inc').write_text(reference)
(OUT/'candidate.inc').write_text(candidate)
program='''#include "citro3d.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u32 commands[4096];unsigned gpuCmdBufOffset;
void GPUCMD_AddWrite(u32 r,u32 v){assert(gpuCmdBufOffset+3<4096);commands[gpuCmdBufOffset++]=r;commands[gpuCmdBufOffset++]=1;commands[gpuCmdBufOffset++]=v;}
void GPUCMD_AddWrites(u32 r,const u32*v,u32 n){assert(gpuCmdBufOffset+n+2<4096);commands[gpuCmdBufOffset++]=r;commands[gpuCmdBufOffset++]=n;memcpy(commands+gpuCmdBufOffset,v,n*4);gpuCmdBufOffset+=n;}
void GPUCMD_AddIncrementalWrites(u32 r,const u32*v,u32 n){GPUCMD_AddWrites(r|0x80000000,v,n);}
void mp_native_panic(const char*s){fprintf(stderr,"%s\\n",s);abort();}
#include "reference.inc"
#include "candidate.inc"
#include "uniform_dispatch.c"
static u32 rng=0x94104c71;static u32 next(void){rng=rng*1664525u+1013904223u;return rng;}
int main(void){
 u32 saved[4096];unsigned checks=0,words=0;
 float24Uniform_s constants[]={{2,{0x12,0x34,0x56}},{95,{0xaa,0xbb,0xcc}}};
 dvle_s dvle[2]={{0},{1}};
 shaderInstance_s sh[2]={{.dvle=dvle,.numFloat24Uniforms=2,.float24Uniforms=constants},{.dvle=dvle+1,.numFloat24Uniforms=2,.float24Uniforms=constants}};
 for(unsigned step=0;step<20000;++step){
  GPU_SHADER_TYPE type=(next()>>20)&1;
  if(step%3==0){unsigned row=(next()>>16)%96;for(unsigned j=0;j<4;++j){u32 v=next();memcpy(&C3D_FVUnif[type][row].values[j],&v,4);}ref_C3D_FVUnif[type][row]=C3D_FVUnif[type][row];C3D_FVUnifDirty[type][row]=ref_C3D_FVUnifDirty[type][row]=true;}
  if(step%13==0){C3D_BoolUnifs[type]=ref_C3D_BoolUnifs[type]=next();C3D_BoolUnifsDirty[type]=ref_C3D_BoolUnifsDirty[type]=true;}
  if(step%67==0){unsigned row=(next()>>16)%4;C3D_IVUnif[type][row]=ref_C3D_IVUnif[type][row]=next();C3D_IVUnifDirty[type][row]=ref_C3D_IVUnifDirty[type][row]=true;}
  if(step%211==0){ref_C3Di_ClearShaderUniforms(type);__wrap_C3Di_ClearShaderUniforms(type);}
  if(step%509==0){sh[type].boolUniformMask=next();sh[type].boolUniforms=next();sh[type].intUniformMask=next()&15;for(unsigned j=0;j<4;++j)sh[type].intUniforms[j]=next();ref_C3Di_LoadShaderUniforms(&sh[type]);__wrap_C3Di_LoadShaderUniforms(&sh[type]);}
  if(step%997==0){ref_C3Di_DirtyUniforms(type);__wrap_C3Di_DirtyUniforms(type);}
  for(unsigned t=0;t<2;++t){
   gpuCmdBufOffset=0;ref_C3D_UpdateUniforms(t);unsigned n=gpuCmdBufOffset;memcpy(saved,commands,n*4);
   gpuCmdBufOffset=0;__wrap_C3D_UpdateUniforms(t);assert(gpuCmdBufOffset==n);assert(!memcmp(saved,commands,n*4));words+=n;++checks;
   assert(!memcmp(C3D_FVUnifDirty,ref_C3D_FVUnifDirty,sizeof(C3D_FVUnifDirty)));
  }
 }
 printf("PASS %u exact SDK command comparisons, %u words, %u clean-stage skips\\n",checks,words,uniform_dispatch_skips[0]+uniform_dispatch_skips[1]);
 assert(uniform_dispatch_skips[0]+uniform_dispatch_skips[1]>20000);
 return 0;
}
'''
(OUT/'test.c').write_text(program)
cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
exe=OUT/'test.exe'
subprocess.run([str(cc),'-O2','-Wall','-Wextra','-DMP_SMOKE_TEST','-I'+str(OUT),'-I'+str(ROOT/'port/3ds'),str(OUT/'test.c'),'-o',str(exe)],check=True)
result=subprocess.check_output([str(exe)],text=True)
(OUT/'result.txt').write_text(result+'pinned_uniforms_sha256='+hashlib.sha256((ROOT/'references/citro3d/source/uniforms.c').read_bytes()).hexdigest()+'\n')
print(result,end='')
