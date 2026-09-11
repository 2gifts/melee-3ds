"""Final controller-driven combat, output and regular Versus checks."""
import json, subprocess, sys, time
from gameplay_test import ROOT, exchange
import select_test_stage as select
from stage_sweep import exit_training, capture


def run(tool, *args):
    log = ROOT/'build'/('update4-final-'+tool.removesuffix('.py')+'.log')
    with log.open('w') as output:
        subprocess.run([sys.executable, str(ROOT/'tools'/tool), *map(str,args)],
                       stdout=output, stderr=subprocess.STDOUT, check=True)
    print('Passed '+tool, flush=True)


def stage(index, versus=False, fresh=False):
    old = sys.argv
    sys.argv = ['select_test_stage.py', str(index), '--character', '10', '--cpu', '16']
    if versus: sys.argv.append('--versus')
    if fresh: sys.argv.append('--fresh')
    try: select.main()
    finally: sys.argv = old


def main():
    # Retest the two stages touched by the implicit-return audit.
    fresh='--fresh' in sys.argv
    for order,index in enumerate((22,21)):
        if not (fresh and order==0):exit_training()
        stage(index,fresh=fresh and order==0)
        select.act(frames=120)
        start=exchange()['simulation'];deadline=time.monotonic()+120
        while exchange()['simulation']<start+3600:
            if time.monotonic()>deadline:raise TimeoutError('Stage simulation did not advance')
            time.sleep(2)
        state=exchange();assert not state['failed'],state
        capture(index)
        (ROOT/f'build/update4-stage-{index}-return-retest.json').write_text(json.dumps(state,indent=2))
        print('Passed stage return retest '+str(index),flush=True)
    # Venom's original callbacks must survive the Arwing spawn/fire cycle.
    exit_training(); stage(11)
    run('cpu_attack_test.py', '--seconds', 60)
    capture(11)
    exit_training(); stage(6)
    run('qol_live_test.py')
    run('cpu_attack_test.py', '--seconds', 30)
    run('display_mode_test.py', '--minimum-side-pixels', 100)
    run('audio_capture_test.py')
    run('verify_live_image.py')
    # Leave Training through its real menus and launch a regular 4-stock,
    # 8-minute match. No fighter/timer/result memory is manufactured.
    state = exit_training()
    for _ in range(4):
        if 'menu' in state: break
        # The original CSS requires holding B for over 30 game updates.
        state = select.act(0x200,40); state = select.act(frames=30)
    else: raise RuntimeError('Training did not return to the main menu')
    select.open_versus(state); stage(6,True)
    from versus_flow_test import observe
    initial = observe()
    assert initial['mode']==2 and 470<=initial['timer']<=480,initial
    (ROOT/'build/update4-default-versus-start.json').write_text(json.dumps(initial,indent=2))
    run('versus_flow_test.py')
    run('versus_return_test.py')
    print('Final update 4 controller/output/Versus checks passed', flush=True)


if __name__=='__main__': main()
