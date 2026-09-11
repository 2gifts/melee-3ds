"""Follow the four-player UI test through Results and Zelda/Sheik Training."""
import json,subprocess,sys,time
from bottom_screen_test import OUT,ROOT,snapshot,capture,choose
import select_test_stage as select

def run(name,*args):
    with (OUT/(name+'.log')).open('w') as log:
        subprocess.run([sys.executable,str(ROOT/'tools'/name),*args],stdout=log,stderr=subprocess.STDOUT,check=True)

def main():
    deadline=time.monotonic()+240
    while snapshot()['scene']!=5:
        if time.monotonic()>deadline:raise TimeoutError('Versus results did not appear')
        assert not snapshot()['engine_failed'];time.sleep(2)
    result=capture('results-final');assert sum(p['kind']!=3 for p in result['players'])==4
    assert result['stock_mode']==1 and result['timer']==1
    assert any(p['stocks']==0 for p in result['players'])
    run('versus_return_test.py')
    for _ in range(5):
        state=select.act(0x200,120);state=select.act(frames=20)
        if 'menu' in state:break
    else:raise RuntimeError('CSS did not return to the main menu')
    state=select.open_mode(state,False)
    for slot,character in [(0,18),(1,14)]:
        icon=next(c['id'] for c in state['characters'] if c['character_kind']==character)
        state=choose(state,slot,icon)
    select.act(0x1000,4);select.act(frames=30)
    old=sys.argv;sys.argv=['select_test_stage.py','25']
    try:select.main()
    finally:sys.argv=old
    select.act(frames=180)
    state=capture('training-zelda');assert state['scene']==4 and not state['stock_mode'] and not state['teams']
    assert [p['kind'] for p in state['players']]==[0,1,3,3]
    assert [p['character'] for p in state['players'][:2]]==[18,14]
    select.act(0x200,3,0,-80);select.act(frames=120)
    state=capture('training-sheik');assert state['players'][0]['character']==19
    select.act(0x200,3,0,-80);select.act(frames=120)
    state=capture('training-zelda-return');assert state['players'][0]['character']==18
    run('cpu_attack_test.py','--seconds','20');run('verify_live_image.py')
    capture('training-combat')
    print('Results, rematch, Training, both climbers, transformation and CPU combat passed.',flush=True)

if __name__=='__main__':main()
