"""Separate deterministic stereo differences from paused-scene variation."""
import hashlib, json
import numpy as np
from PIL import Image
from gameplay_test import ROOT, TEST_ELF
from menu_display_pause_test import clock_state
from profile_switch import set_word
from efb_copy_test import SD
import bottom_screen_test as bottom
import select_test_stage as select


def main():
    bottom.OUT=ROOT/'build/stereo-reuse-game-qa/repeat'
    bottom.OUT.mkdir(parents=True,exist_ok=True)
    initial=clock_state();assert initial['pause_flags']
    pictures=[]
    try:
        for index,disabled in enumerate((1,1,0,0,1)):
            set_word('stereo_reuse_disable',disabled)
            select.act(frames=20)
            name=f'{index}-'+('reference' if disabled else 'reuse')
            bottom.capture(name)
            right=np.rot90(np.fromfile(SD/'engine-right.bgr',dtype=np.uint8).reshape(400,240,3)[:,:,::-1]).copy()
            Image.fromarray(right).save(bottom.OUT/(name+'-right.png'))
            pictures.append([np.array(Image.open(bottom.OUT/(name+'-top.png'))),right])
            assert clock_state()['match_frame']==initial['match_frame']
        comparisons=[]
        for a,b in ((0,1),(2,3),(0,4),(1,2),(3,4)):
            differences=[np.abs(x.astype(np.int16)-y.astype(np.int16)) for x,y in zip(pictures[a],pictures[b])]
            comparisons.append(dict(pair=[a,b],changed_channels=[int(np.count_nonzero(d)) for d in differences],maximum_error=[int(d.max()) for d in differences]))
        result=dict(comparisons=comparisons,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
                    clock=initial,physical_fps_verified=False)
        (bottom.OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
    finally:set_word('stereo_reuse_disable',1)


if __name__=='__main__':main()
