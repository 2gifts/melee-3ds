"""Compare shared source snapshots against separate prefixes in a paused match."""
import json,time
import numpy as np
from PIL import Image
import select_test_stage as select
import bottom_screen_test as bottom
from menu_display_pause_test import clock_state
from profile_switch import set_word
from profile_render_detail import snapshot,summarize


def rebuild(grow):
    set_word('geometry_source_grow',grow,'big')
    # The original GX invalidation boundary safely clears all cache owners.
    set_word('geometry_range_share',0,'big');select.act(frames=5)
    set_word('geometry_range_share',1,'big');select.act(frames=120)


def main():
    bottom.OUT=bottom.ROOT/'build/update15-qa';bottom.OUT.mkdir(exist_ok=True)
    report=bottom.OUT/'source-cache-gpu.json'
    if report.exists():report.unlink()
    assert bottom.snapshot()['scene']==2 and clock_state()['pause_flags']
    frozen=clock_state()['match_frame'];set_word('mp_test_stereo_slider',1000)
    profiles=[];images={}
    try:
        for iteration in range(3):
            for grow in (0,1):
                rebuild(grow)
                start=snapshot(True);time.sleep(3);end=snapshot(False)
                assert clock_state()['match_frame']==frozen,'Match advanced during comparison'
                profiles.append(dict(growth=bool(grow),iteration=iteration,**summarize(start,end)))
                if iteration==0:
                    label='source-grown' if grow else 'source-separate'
                    bottom.capture(label)
                    left=np.array(Image.open(bottom.OUT/(label+'-top.png')))
                    assert np.count_nonzero(left)>5000,'Captured scene is blank'
                    sd=bottom.ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
                    images[grow]=[left,np.fromfile(sd/'engine-right.bgr',dtype=np.uint8).copy()]
        differences=[int(np.count_nonzero(a!=b)) for a,b in zip(images[0],images[1])]
        assert differences==[0,0],('Stereo channel differences',differences)
        result=dict(passed=True,different_channels=differences,stereo_slider=1000,profiles=profiles,
            timing_scope='Same paused scene in Azahar; CPU phases and bytes checked, not console FPS')
        report.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result),flush=True)
    finally:
        set_word('geometry_source_grow',1,'big');set_word('geometry_range_share',1,'big')


if __name__=='__main__':main()
