"""Verify the exact BE8 coefficient predicate, including rejected near misses."""
import subprocess
from build import ROOT,local_clang
exe=ROOT/'build/unit-attenuation-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/unit_attenuation_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
