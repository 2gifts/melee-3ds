"""Audit the retail copied-model layout and exercise the production padding helper."""
import json,subprocess
from pathlib import Path
from audit_diet_fountain import Archive
from assets import ROOT,u32
from engine_overlays import adapt
from build import local_clang


def main():
    a=Archive(ROOT/'assets/GALE01/files/PlKbCpGw.dat')
    root=a.roots['ftDataKirbyCopyGamewatch'];groups=a.word(root);table=a.word(root+24)
    b=Archive(ROOT/'assets/GALE01/files/PlKb.dat');body=b.word(b.word(b.roots['ftDataKirby']+8))
    assert groups==1 and body==2
    reloc={u32(a.raw,32+len(a.b)+4*i) for i in range(a.inv['relocations'])}
    # The would-be second count is actually an archive pointer. A retail
    # 0x80/0x81... relocation is signed negative; a native 0x08/0x09... is not.
    extra=table+groups*8;assert extra in reloc
    old=a.word(extra)+0x81000000;new=a.word(extra)+0x08000000
    assert old&0x80000000 and not new&0x80000000
    source=adapt(ROOT/'upstream/melee/src/melee/ft/kinds/ftKirby/ftkirby.c').read_text()
    start=source.index('static FtPartsVisLookup mp_kirby_body_vis')
    helper=source[start:source.index('\nvoid ftKb_SpecialN_800F14B4',start)]
    pre='''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef struct {int x0; void*x4;} FtPartsVisLookup;
typedef struct {unsigned model_num;} Vis;
typedef struct {unsigned player_id;Vis x5AC;struct{struct{struct{Vis x24;}hat;}kb;}u;} Fighter;
#define HSD_ASSERT(line,value) assert(value)
'''
    test='''
int main(void){
 unsigned tests=0;FtPartsVisLookup source[11],saved[6][11];
 for(unsigned i=0;i<11;++i)source[i]=(FtPartsVisLookup){(int)i+1,(void*)(uintptr_t)(0x1000+i*16)};
 for(unsigned body=0;body<=11;++body)for(unsigned hat=0;hat<=11;++hat){
  for(unsigned slot=0;slot<6;++slot){
   memcpy(saved,mp_kirby_body_vis,sizeof(saved));
   Fighter fp={.player_id=slot,.x5AC={body},.u={.kb={.hat={.x24={hat}}}}};
   FtPartsVisLookup*out=mp_kirby_body_visibility(&fp,source);
   assert(out==mp_kirby_body_vis[slot]);
   for(unsigned i=0;i<11;++i){
    if(i<body&&i<hat)assert(out[i].x0==source[i].x0&&out[i].x4==source[i].x4);
    else assert(out[i].x0==0&&out[i].x4==NULL);
   }
   for(unsigned i=0;i<6;++i)if(i!=slot)assert(!memcmp(saved[i],mp_kirby_body_vis[i],sizeof(saved[i])));
   ++tests;
  }
 }
 printf("%u\\n",tests);return 0;
}
'''
    directory=ROOT/'build/update15-kirby-host';directory.mkdir(exist_ok=True)
    c=directory/'test.c';exe=directory/'test.exe';c.write_text(pre+helper+test)
    subprocess.run([str(local_clang()),'-O2',str(c),'-o',str(exe)],check=True)
    checks=int(subprocess.check_output([str(exe)]))
    result=dict(passed=True,slot_and_count_cases=checks,hat_groups=groups,body_groups=body,
        copied_model_file='PlKbCpGw.dat',extra_group_count_is_relocation=True,
        retail_extra_count=hex(old),native_extra_count=hex(new))
    (ROOT/'build/update15-qa/kirby-visibility-host.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
