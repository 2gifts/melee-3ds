"""Check profiler accounting before trusting its timing breakdown."""
import subprocess
from build import ROOT,local_clang
out=ROOT/'build/feasibility-host';out.mkdir(parents=True,exist_ok=True)
source=(ROOT/'port/engine/feasibility.c').read_text()
for line in ('#include "native.h"','#include <dolphin/os.h>','#include <sysdolphin/baselib/gobj.h>'):
    source=source.replace(line,'')
prefix='''#define MP_FEASIBILITY_TEST
#include <assert.h>
#include <stdio.h>
typedef struct HSD_GObj {unsigned char p_link,gx_link;void (*render_cb)(struct HSD_GObj*,int);} HSD_GObj;
static unsigned fake_tick;
static unsigned mp_platform_ticks(void){return fake_tick;}
static void OSReport(const char*f,...){(void)f;}
'''
test='''
static void render(HSD_GObj*g,int pass){assert(mp_probe_drop(1));assert(!mp_probe_drop(2));fake_tick+=7;}
int main(void){
  assert(!mp_probe_begin(1,100,0));mp_probe_enabled=1;
  unsigned a=mp_probe_begin(1,100,0);fake_tick=10;
  unsigned b=mp_probe_begin(2,200,0);fake_tick=30;mp_probe_end(b);
  fake_tick=40;mp_probe_end(a);unsigned found=0;
  for(unsigned i=0;i<1024;i++){ProbeRow*r=mp_probe_rows+i;
    if(r->function==100){assert(r->inclusive==40&&r->self==20&&r->calls==1);++found;}
    if(r->function==200){assert(r->inclusive==20&&r->self==20);++found;}}
  assert(found==2&&!mp_probe_errors);
  fake_tick=0xfffffff0;a=mp_probe_begin(3,300,0);fake_tick=0x10;mp_probe_end(a);
  for(unsigned i=0;i<1024;i++)if(mp_probe_rows[i].function==300)assert(mp_probe_rows[i].inclusive==32);
  mp_probe_mode=1;mp_probe_links=1u<<7;HSD_GObj g={7,3,render};mp_probe_render(&g,0);
  assert(!mp_probe_drop(1));assert(!mp_probe_depth);
  mp_probe_mode=0;mp_probe_frames=30;mp_probe_request=1;mp_probe_checkpoint();
  for(unsigned i=0;i<30;i++){fake_tick+=40500;mp_probe_checkpoint();}
  assert(mp_probe_done==1&&!mp_probe_enabled&&!mp_probe_result[6]&&!mp_probe_result[7]);
  assert(mp_probe_result[2]==0&&mp_probe_result[3]==30*40500);
  assert(mp_probe_sampled_frames==30);
  mp_probe_stride=8;mp_probe_frames=120;mp_probe_request=2;mp_probe_checkpoint();
  for(unsigned i=0;i<120;i++){
    unsigned t=mp_probe_begin(3,555,0);fake_tick+=40500;mp_probe_end(t);mp_probe_checkpoint();
  }
  assert(mp_probe_done==2&&mp_probe_sampled_frames==15);
  for(unsigned i=0;i<1024;i++)if(mp_probe_rows[i].function==555)assert(mp_probe_rows[i].calls==15);
  mp_probe_stride=0;mp_probe_request=3;mp_probe_checkpoint();assert(mp_probe_done==3&&mp_probe_errors==1&&!remaining);
  mp_probe_errors=0;mp_probe_stride=1;
  mp_probe_enabled=1;unsigned tokens[32];
  for(unsigned i=0;i<32;i++)tokens[i]=mp_probe_begin(1,400+i,0);
  assert(!mp_probe_begin(1,999,0)&&mp_probe_errors==1);
  for(unsigned i=32;i;i--)mp_probe_end(tokens[i-1]);assert(!mp_probe_depth);
  puts("Profiler checks passed: nested exclusive accounting, zero/wrapped clock, callback preservation, category selection, bounded frame completion and depth overflow.");
}
'''
path=out/'test.c';path.write_text(prefix+source+test)
exe=out/'test.exe'
subprocess.run([local_clang(),'-O2','-I'+str(ROOT/'port/engine'),str(path),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],text=True,capture_output=True);(out/'result.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
auto_test='''
static unsigned calls;
static void callback(void* object){(void)object;assert(!mp_probe_drop(1));assert(!mp_probe_drop(2));++calls;fake_tick+=100;}
static void render(HSD_GObj*g,int pass){(void)g;(void)pass;callback(g);}
int main(void){
  HSD_GObj g={5,3,render};mp_probe_mode=2;mp_probe_links=~0u;mp_probe_enabled=1;
  mp_probe_proc(&g,callback);mp_probe_render(&g,0);assert(calls==2&&!mp_probe_depth);
  mp_probe_enabled=0;mp_probe_mode=0;mp_probe_scene(2);
  for(unsigned i=0;i<121;i++){fake_tick+=40500;mp_probe_checkpoint();}
  assert(remaining==120&&mp_probe_enabled&&mp_probe_request==1);
  for(unsigned i=0;i<120;i++){
    mp_probe_proc(&g,callback);fake_tick+=40500;mp_probe_checkpoint();
  }
  assert(mp_probe_done==1&&mp_probe_sampled_frames==18&&auto_captures==1&&!mp_probe_enabled);
  unsigned found=0;for(unsigned i=0;i<1024;i++)if(mp_probe_rows[i].calls){assert(mp_probe_rows[i].calls==18);++found;}
  assert(found==1&&!mp_probe_errors&&!mp_probe_overflow);
  for(unsigned i=0;i<1100;i++){fake_tick+=40500;mp_probe_checkpoint();}
  assert(auto_captures==3&&!remaining&&!mp_probe_enabled);
  unsigned done=mp_probe_done;for(unsigned i=0;i<1000;i++)mp_probe_checkpoint();assert(mp_probe_done==done);
  mp_probe_scene(2);for(unsigned i=0;i<121;i++)mp_probe_checkpoint();assert(remaining);
  mp_probe_scene(9);assert(!remaining&&!mp_probe_enabled&&mp_probe_done==mp_probe_request);
  for(unsigned i=0;i<1000;i++)mp_probe_checkpoint();assert(!remaining);
  puts("Automatic capture checks passed: sparse samples, original callbacks retained, no omissions, three bounded windows, transition discard, no menu profiling.");
}
'''
path=out/'auto.c';path.write_text('#define MP_FEASIBILITY_AUTO\n'+prefix+source+auto_test)
exe=out/'auto.exe'
subprocess.run([local_clang(),'-O2','-I'+str(ROOT/'port/engine'),str(path),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],text=True,capture_output=True);(out/'auto-result.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
