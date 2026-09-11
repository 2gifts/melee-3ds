"""Compare the optimized tiler against the renderer's actual original decoder."""
import subprocess
from build import ROOT,local_clang
source=(ROOT/'port/3ds/renderer.c').read_text()
begin=source.index('static u32 rgba(');end=source.index('static unsigned texture_bucket(',begin)
generated=ROOT/'build/generated/texture_reference.inc';generated.write_text(source[begin:end])
exe=ROOT/'build/texture-repack-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-I'+str(generated.parent),
    str(ROOT/'tests/texture_repack_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
