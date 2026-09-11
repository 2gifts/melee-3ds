"""Exercise the actual adapted particle cleanup with nonadjacent globals."""
import subprocess
from build import ROOT,UPSTREAM,local_clang
from engine_overlays import adapt

source=adapt(UPSTREAM/'src/sysdolphin/baselib/particle.c').read_text()
start=source.index('void hsd_8039D0A0(HSD_Generator* gen)\n{')
end=source.index('\n}\n',start)+2
generated=ROOT/'build/generated';generated.mkdir(parents=True,exist_ok=True)
(generated/'particle_cleanup_function.inc').write_text(source[start:end])
exe=ROOT/'build/particle-cleanup-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-I'+str(generated),str(ROOT/'tests/particle_cleanup_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
