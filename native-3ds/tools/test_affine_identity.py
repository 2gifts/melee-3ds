"""Check exact BE8 coefficient classification for the GPU color shortcut."""
import subprocess
from build import ROOT,local_clang
out=ROOT/'build/affine-identity-test';out.mkdir(parents=True,exist_ok=True)
exe=out/'test.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/affine_identity_tests.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,text=True,capture_output=True)
(out/'result.txt').write_text(result.stdout)
print(result.stdout,end='')
