"""Exercise actual generated queue functions under device-completion schedules."""
import hashlib,re,subprocess
from pathlib import Path
from async_renderqueue import ROOT,generate,PIN
out=ROOT/'build/async-queue-host'
candidate=generate(out);source=candidate.read_text()
source=re.sub(r'^#include .*$', '',source,flags=re.M)
# Host stubs replace only drawing/allocation device boundaries. Keep the
# original frame submission, callback, wait and safe-transfer functions.
start=source.index('static C3D_RenderTarget* C3Di_RenderTargetNew(void)')
end=source.index('static void C3Di_SafeDisplayTransfer(',start)
source=source[:start]+source[end:]
(out/'queue-under-test.inc').write_text(source)
early=re.sub(r'^#include .*$', '',(ROOT/'port/3ds/early_queue.c').read_text(),flags=re.M)
(out/'early-queue-under-test.inc').write_text(early)
cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe';exe=out/'test.exe'
subprocess.run([str(cc),'-O2','-Wall','-Wextra','-I'+str(out),str(ROOT/'tests/async_queue.c'),'-o',str(exe)],check=True)
result=subprocess.check_output([str(exe)],text=True)
# Negative control: the original SDK lacks publication/join protection when
# prefixes run early. The same schedules must detect its incomplete swaps.
reference=(ROOT/'references/citro3d/source/renderqueue.c').read_text()
reference=re.sub(r'^#include .*$', '',reference,flags=re.M)
start=reference.index('static C3D_RenderTarget* C3Di_RenderTargetNew(void)')
end=reference.index('static void C3Di_SafeDisplayTransfer(',start)
reference=('static bool publishing;\nstatic unsigned mp_queue_deferred_callbacks,mp_queue_stale_callbacks,mp_queue_join_completions,mp_queue_publications;\n'+reference[:start]+reference[end:])
refdir=out/'reference';refdir.mkdir(exist_ok=True)
(refdir/'queue-under-test.inc').write_text(reference)
(refdir/'early-queue-under-test.inc').write_text(early)
refexe=refdir/'test.exe'
subprocess.run([str(cc),'-O2','-DMP_QUEUE_REFERENCE_TEST','-Wno-int-to-void-pointer-cast','-I'+str(refdir),str(ROOT/'tests/async_queue.c'),'-o',str(refexe)],check=True)
negative=subprocess.run([str(refexe)],capture_output=True,text=True)
assert negative.returncode==86 and any(x in negative.stderr for x in ('copied[','swaps[','stereo==')),negative
(out/'reference-failure.txt').write_text(negative.stderr)
(out/'result.txt').write_text(result+'Original SDK negative control: '+negative.stderr+'pinned_sdk_sha256='+PIN+'\ncandidate_sha256='+hashlib.sha256(candidate.read_bytes()).hexdigest()+'\n')
print(result,end='')
print('Original SDK negative control detected:',negative.stderr.strip())
