"""Classify captured colors with the current affine/clamped compilers."""
import argparse,json,subprocess
from build import ROOT,local_clang
ap=argparse.ArgumentParser();ap.add_argument('capture');ap.add_argument('--output',required=True);a=ap.parse_args()
trace=json.loads((ROOT/a.capture).read_text());out=ROOT/a.output;out.mkdir(parents=True,exist_ok=True)
fixture=[]
for key,n in trace['usage']:
    m=trace['materials'][key];alpha=[]
    for ch in range(2):
        c=m['channel_configuration'][(2+ch)*6:(3+ch)*6]
        alpha.append(m['material'][ch*4+3]/255 if not c[0] and (c[2]==0 or not m['descriptors'][11+ch]) else -1)
    cfg=m['tev_configuration'];arrays=lambda xs:'{'+','.join(map(str,xs))+'}'
    fixture.append('{"'+key+'",'+str(n)+','+str(m['num_stages'][0])+','+arrays([arrays(cfg[i*30:i*30+30]) for i in range(16)])+','+arrays(m['tev_color'])+','+arrays(m['konst_color'])+','+arrays(alpha)+'}')
source='''#include <stdio.h>
#include <math.h>
#include "gx_shade.h"
#include "clamped_shade.h"
typedef struct {const char*id;unsigned vertices,stages,config[16][30];unsigned char colors[16],konst[16];float alpha[2];} Fixture;
static const Fixture fixtures[]={'''+','.join(fixture)+'''};
int main(void){for(unsigned i=0;i<sizeof(fixtures)/sizeof(*fixtures);i++){
 const Fixture*f=fixtures+i;float c[4][4],k[4][4];for(unsigned x=0;x<4;x++)for(unsigned y=0;y<4;y++){c[x][y]=f->colors[x*4+y]/255.f;k[x][y]=f->konst[x*4+y]/255.f;}
 MPShadePlan affine={0};MPClampedPlan clamped={0};mp_shade_compile_channels(&affine,f->config,f->stages,c,k,f->alpha);mp_clamped_compile(&clamped,f->config,f->stages,c,k,f->alpha);
 printf("%s vertices=%u affine=%u clamped=%u channels=%u\\n",f->id,f->vertices,affine.valid,clamped.valid,1+clamped.second);
 if(clamped.valid){for(unsigned j=0;j<4;j++)printf(" component%u inner=%g,%g,%g,%g,%g clamp=%u scale=%g bias=%g\\n",j,clamped.coefficients[0][j],clamped.coefficients[1][j],clamped.coefficients[2][j],clamped.coefficients[3][j],clamped.coefficients[4][j],clamped.clamp[j],clamped.scale[j],clamped.bias[j]);}
}return 0;}
'''
(out/'inspect.c').write_text(source);exe=out/'inspect.exe'
subprocess.run([local_clang(),'-O2','-ffp-contract=off','-I'+str(ROOT/'port/engine'),str(out/'inspect.c'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);(out/'result.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
