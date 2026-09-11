"""Test the production UCF functions against isolated fighter/SDK inputs."""
import subprocess
from build import ROOT,local_clang
source=(ROOT/'port/engine/ucf.c').read_text()
generated=ROOT/'build/generated/ucf_body.inc'
generated.write_text(source[source.index('static MPUcfPad pads'):])
exe=ROOT/'build/ucf-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-include','stdlib.h',
    '-I'+str(generated.parent),str(ROOT/'tests/ucf_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
