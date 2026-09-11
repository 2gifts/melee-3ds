"""Exercise cache ordering independently of GPU allocation and drawing."""
import subprocess
from build import ROOT,local_clang
exe=ROOT/'build/cache-lru-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/cache_lru_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
