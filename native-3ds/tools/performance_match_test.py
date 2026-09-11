"""Enter and pause a stereo Versus match using original controller input.

Scene and player state are read only. A paused scene provides reproducible
pixels for rendering A/B tests without stock losses during GDB inspection.
"""
import argparse,json,sys
import select_test_stage as select
import bottom_screen_test as bottom
from profile_switch import set_word
from menu_display_pause_test import clock_state

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--stage',type=int,default=32)
    ap.add_argument('--players',type=int,default=2,choices=(2,4))
    ap.add_argument('--output',default='build/update15-qa');args=ap.parse_args()
    bottom.OUT=bottom.ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    for _ in range(90):
        state=select.observe()
        if 'menu' in state:break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2)
        select.act(frames=25)
    else:raise AssertionError('Main menu did not open')
    state=select.open_versus(state)
    for slot,icon in enumerate((1,6,16,23)[:args.players]):state=bottom.choose(state,slot,icon)
    select.act(0x1000,4);state=select.act(frames=40)
    index=next(row['id'] for row in state['icons'] if row['stage_kind']==args.stage)
    saved=sys.argv;sys.argv=['select_test_stage.py',str(index)]
    try:select.main()
    finally:sys.argv=saved
    select.act(frames=130)
    for _ in range(5):
        if clock_state()['pause_flags']:break
        select.act(0x1000,3);select.act(frames=25)
    frozen=clock_state();assert frozen['pause_flags'],frozen
    state=bottom.capture('performance-match');assert state['scene']==2,state
    assert sum(p['kind']!=3 for p in state['players'])==args.players,state
    print(json.dumps({'paused':frozen,'state':state}),flush=True)

if __name__=='__main__':main()
