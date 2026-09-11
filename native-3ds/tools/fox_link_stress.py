"""Exercise Fox special effects against an attacking Link with pad input only."""
import json
from collections import Counter
from gameplay_test import ROOT,exchange
from combat_test import act
from cpu_attack_test import set_cpu_behavior

def main():
    initial=exchange();assert [f['kind'] for f in initial['fighters'][:2]]==[1,6],initial
    set_cpu_behavior(4);samples=[]
    for _ in range(12):
        state=exchange();direction=-1 if state['fighters'][0]['position'][0]>0 else 1
        for buttons,frames,x,y in [(0x200,3,80*direction,0),(0,55,0,0),
                                   (0x200,4,0,-80),(0,25,0,0),(0x200,3,0,0),(0,25,0,0)]:
            samples.append(act(buttons,frames,x,y))
    motions=Counter(s['fighters'][0]['motion'] for s in samples)
    result={'initial':initial,'motion_samples':dict(motions),'samples':samples,'game_state_memory_modified':False}
    (ROOT/'build/fox-link-special-stress.json').write_text(json.dumps(result,indent=2)+'\n')
    # Fox's retail side-special state family: start/travel/end, ground/air.
    assert any(motions[m] for m in range(347,353)),motions
    assert any(motions[m] for m in (360,361,362,363,364,365,366,367,368,369)),motions
    print('Fox side-special and reflector states observed; twelve attack cycles completed.')

if __name__=='__main__':main()
