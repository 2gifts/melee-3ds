"""Validate shared caches in the paused four-fighter Temple test encounter."""
import json,subprocess,sys
import bottom_screen_test as bottom
import select_test_stage as select
from menu_display_pause_test import clock_state


def run(name,*args):
    with (bottom.ROOT/'build'/('update15-'+name+'.log')).open('w') as log:
        subprocess.run([sys.executable,str(bottom.ROOT/'tools'/args[0]),*args[1:]],
            stdout=log,stderr=subprocess.STDOUT,check=True)
    print(name+' passed',flush=True)


def gameplay():
    state=bottom.snapshot()
    assert state['scene']==2 and state['stage']==14 and not state['engine_failed'],state
    assert sum(p['kind']!=3 for p in state['players'])==4,state
    return state


def main():
    out=bottom.ROOT/'build/update15-qa'
    gameplay();assert clock_state()['pause_flags']
    run('source-cache-ab','source_cache_gpu_test.py')
    run('casual-render-ab','render_update15_test.py')
    (out/'casual-render-ab.json').write_bytes((out/'render-ab.json').read_bytes())
    initial=gameplay();select.act(0x1000,3);select.act(frames=25)
    assert not clock_state()['pause_flags']
    run('native-cache-qa','native_geometry_test.py','--seconds','3')
    final=gameplay();checks=json.loads((bottom.ROOT/'build/native-geometry-test.json').read_text())
    result=dict(passed=True,initial=initial,final=final,checks=checks)
    (out/'casual-cache-live.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Animated four-fighter geometry validated against fresh decoding',flush=True)


if __name__=='__main__':main()
