"""Rendering cost ceilings in a temporarily frozen original gameplay scene.

All HSD callbacks are still invoked. Original object process flags are
restored afterward. Graphics omissions are diagnostic, never shipping modes.
"""
import argparse,json
import numpy as np
from gameplay_test import ROOT
from results_capture_scene_test import frozen_processes,capture
from profile_switch import set_word
from run_feasibility import measure
import bottom_screen_test as bottom
import select_test_stage as select

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);ap.add_argument('--frames',type=int,default=120);a=ap.parse_args()
    out=ROOT/a.output;out.mkdir(parents=True,exist_ok=True);bottom.OUT=out
    initial=bottom.snapshot();assert initial['scene']==2 and not initial['engine_failed'],initial
    rows=[]
    try:
        with frozen_processes() as count:
            select.act(frames=10);original=capture('original');repeat=capture('original-repeat')
            stable=[int(np.count_nonzero(x!=y)) for x,y in zip(original,repeat)]
            (out/'fixture.json').write_text(json.dumps(dict(initial=initial,processes_suspended=count,stability_channels=stable),indent=2))
            assert stable==[0,0],('Scene still changes; do not use for paired timing',stable)
            assert all(np.count_nonzero(x)>5000 for x in original)
            # Verified in melee/gr/forward.h and melee/ft/forward.h.
            # Ground objects use p_link 5; fighters use 8.
            modes=[('baseline-a',0,0xffffffff,0),('profile',0,0xffffffff,1),
                   ('no-native-draws',1,0xffffffff,0),('no-display-lists',2,0xffffffff,0),
                   ('no-stage-lists',2,1<<5,0),('no-fighter-lists',2,1<<8,0),
                   ('no-display-lists-repeat',2,0xffffffff,0),('baseline-b',0,0xffffffff,0)]
            for label,mode,links,profile in modes:
                assert bottom.snapshot()['scene']==2,'Original scene changed'
                set_word('mp_probe_links',links,'big');set_word('mp_probe_mode',mode,'big')
                select.act(frames=12)
                if label in ('no-stage-lists','no-fighter-lists'):capture(label)
                row=measure(out/label,a.frames,profile,mode,links);rows.append(dict(label=label,**row))
                (out/'progress.json').write_text(json.dumps(rows,indent=2))
            select.act(frames=12);restored=capture('restored')
            differences=[int(np.count_nonzero(x!=y)) for x,y in zip(original,restored)]
            assert differences==[0,0],('Baseline did not restore',differences)
            (out/'summary.json').write_text(json.dumps(dict(rows=rows,restored_channels=differences,
                physical_throughput_verified=False,scope='Frozen scene; no claim of simulation speed or shippable visual quality'),indent=2))
    finally:
        set_word('mp_probe_mode',0,'big');set_word('mp_probe_enabled',0,'big')

if __name__=='__main__':main()
