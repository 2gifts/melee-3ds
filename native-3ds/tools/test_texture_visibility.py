"""Check encoded image extents and overlapping image/palette invalidation."""
import subprocess
from build import ROOT,local_clang
exe=ROOT/'build/texture-visibility-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/texture_visibility_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
