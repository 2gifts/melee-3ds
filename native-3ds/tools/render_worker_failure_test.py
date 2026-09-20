"""Verify worker panic handoff through the original main-thread recovery path.

Run only after gameplay/visual tests: this intentionally stops the engine.
It cannot establish handling of a CPU exception or a physical GPU hang.
"""
import argparse,hashlib,json,time
from gameplay_test import ROOT,TEST_ELF
from profile_switch import set_word
from texture_visibility_test import read
from efb_copy_test import SD

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);args=ap.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True)
    names=('engine_failed','mp_render_worker_active','mp_render_worker_failures','mp_render_worker_jobs')
    initial=read(names)
    assert initial['engine_failed']==0 and initial['mp_render_worker_active']==1,initial
    set_word('mp_render_worker_disable',0);set_word('mp_render_worker_test_failure',1)
    deadline=time.monotonic()+60
    while True:
        time.sleep(.25);final=read(names)
        if final['engine_failed']:break
        assert time.monotonic()<deadline,('Worker failure did not reach main recovery',final)
    assert final['mp_render_worker_active']==0,final
    assert final['mp_render_worker_failures']==initial['mp_render_worker_failures']+1,final
    # The main panic path flushes after reporting and before engine_failed.
    log=(SD/'game.log').read_text(encoding='utf-8',errors='replace')
    assert 'Injected renderer worker failure' in log
    (out/'game.log').write_text(log,encoding='utf-8')
    result=dict(passed=True,initial=initial,final=final,
        elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
        scope='Deliberate worker-side panic, joined worker and original main-thread recovery; no cross-thread longjmp',
        physical_hardware_verified=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)

if __name__=='__main__':main()
