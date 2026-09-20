"""Check draw-buffer ownership before introducing asynchronous rendering."""
import subprocess
from build import ROOT,local_clang
out=ROOT/'build/render-snapshot-test';out.mkdir(parents=True,exist_ok=True)
exe=out/'test.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/render_snapshot_tests.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,text=True,capture_output=True,timeout=60)
(out/'result.txt').write_text(result.stdout)
print(result.stdout,end='')
exe=out/'source-cache-test.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra',str(ROOT/'tests/source_snapshot_cache_tests.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,text=True,capture_output=True,timeout=60)
(out/'source-cache-result.txt').write_text(result.stdout)
print(result.stdout,end='')
