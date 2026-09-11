"""Exercise the actual stream wait and I/O pump with deferred completions."""
import subprocess
from build import ROOT,UPSTREAM,local_clang
from engine_overlays import adapt
def function(text,token):
    start=text.index(token);cursor=text.index('{',start)+1;depth=1
    while depth:
        if text[cursor]=='{':depth+=1
        elif text[cursor]=='}':depth-=1
        cursor+=1
    return text[start:cursor]
source=adapt(UPSTREAM/'src/sysdolphin/baselib/synth.c').read_text()
wait=function(source,'while (HSD_Synth_804D7778 != 0)')
pump=function((ROOT/'port/engine/scheduler.c').read_text(),'void mp_engine_io_poll(void)')
generated=ROOT/'build/generated/stream_wait.inc'
generated.write_text(pump+'\nstatic void stream_wait(void){'+wait+'}\n')
exe=ROOT/'build/stream-wait-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-I'+str(generated.parent),str(ROOT/'tests/stream_wait_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
