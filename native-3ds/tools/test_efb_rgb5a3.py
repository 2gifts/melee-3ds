"""Validate RGB5A3 copy bytes, every alpha value, crop/edge/padding guards."""
import subprocess
from build import ROOT,local_clang
exe=ROOT/'build/efb-rgb5a3-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-DMP_TEST_RGB5A3',str(ROOT/'tests/efb_rgb565_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
