"""Exercise the actual renderer transport against a threaded host driver model."""
import subprocess
from build import ROOT,local_clang
out=ROOT/'build/render-worker-host';out.mkdir(parents=True,exist_ok=True)
exe=out/'test.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-DMP_RENDER_WORKER','-DMP_SMOKE_TEST',
    '-I'+str(ROOT/'tests/render_worker_host'),str(ROOT/'tests/render_worker_tests.c'),
    str(ROOT/'port/3ds/render_worker.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=120)
(out/'result.txt').write_text(result.stdout)
print(result.stdout,end='')
exe=out/'contract-test.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-DMP_RENDER_WORKER','-DMP_SMOKE_TEST','-DMP_TEST_GEOMETRY_CONTRACT',
    '-I'+str(ROOT/'tests/render_worker_host'),str(ROOT/'tests/render_worker_tests.c'),
    str(ROOT/'port/3ds/render_worker.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=120)
(out/'contract-result.txt').write_text(result.stdout)
print('Versioned geometry contract and retirement fence: '+result.stdout,end='')
