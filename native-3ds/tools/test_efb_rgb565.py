"""Check RGB565 framebuffer copying against an independent scalar reference."""
import subprocess
from build import ROOT,local_clang
exe=ROOT/'build/efb-rgb565-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/efb_rgb565_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
