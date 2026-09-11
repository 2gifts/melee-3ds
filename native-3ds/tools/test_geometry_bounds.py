import subprocess
from build import ROOT,local_clang
exe=ROOT/'build/geometry-bounds-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/geometry_bounds_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
