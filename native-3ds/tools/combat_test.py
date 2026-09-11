"""Approach and attack the Training CPU using only bounded pad input."""
import json,time
from gameplay_test import ROOT,exchange

records=[]
def act(buttons,frames,x=0,y=0):
    start=exchange((buttons,frames,x,y));deadline=time.monotonic()+120
    while True:
        time.sleep(.25);state=exchange()
        if state['failed']:raise RuntimeError(state)
        if time.monotonic()>deadline:raise TimeoutError('Game frames stopped')
        if state['frame']>=start['frame']+frames+1:break
    records.append({'input':[buttons,frames,x,y],'state':state})
    print(json.dumps(records[-1]),flush=True)
    return state

def main():
    state=exchange();initial=state['fighters'][1]['damage'];records.append({'initial':state})
    for i in range(80):
        p,c=state['fighters'][:2];dx=c['position'][0]-p['position'][0];dy=c['position'][1]-p['position'][1]
        if c['damage']>initial:
            act(0,5);print('Confirmed original-engine opponent damage after controller attacks',flush=True);return
        if p['motion']<11:state=act(0,10);continue
        direction=1 if dx>=0 else -1
        if abs(dx)>14:
            state=act(0,min(6,max(1,int((abs(dx)-10)/2))),direction*60)
        elif dy>14 and not p['airborne']:
            state=act(0x400,2,direction*25);state=act(0,5)
        elif dy< -18 and not p['airborne']:
            state=act(0,3,0,-80);state=act(0,3)
        elif abs(dy)<=18:
            state=act(0x100,3,direction*25);state=act(0,8)
        else:
            state=act(0,4)
    raise RuntimeError('No confirmed hit within controller budget')

if __name__=='__main__':
    try:main()
    finally:(ROOT/'build/combat-test.json').write_text(json.dumps(records,indent=2))
