"""Exercise real match rendering with both eyes and slider/display changes."""
import argparse,json,time,subprocess,sys
import numpy as np
from PIL import Image
from gameplay_test import ROOT,exchange
from efb_copy_test import SD
from texture_visibility_test import read
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
import select_test_stage as select
from stage_sweep import exit_training

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--skip-selection',action='store_true');args=ap.parse_args()
    original=read(('mp_test_stereo_slider','framebuffer_range_disable'))
    records=[]
    try:
        set_word('framebuffer_range_disable',0)
        if not args.skip_selection:
            if 'hand' not in select.observe():exit_training()
            subprocess.run([sys.executable,str(ROOT/'tools/select_test_stage.py'),'19','--character','23','--cpu','6'],check=True)
        for depth,expanded in ((0,0),(1000,0),(500,1),(1000,1),(0,0)):
            set_word('mp_test_stereo_slider',depth);set_word('mp_native_expanded',expanded)
            select.act(frames=120);start=snapshot(True);time.sleep(3);end=snapshot(False)
            subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True)
            images=[]
            for eye,name in enumerate(('engine-top.bgr','engine-right.bgr')):
                im=np.rot90(np.fromfile(SD/name,dtype=np.uint8).reshape(400,240,3)[:,:,::-1]);images.append(im)
                Image.fromarray(im).save(ROOT/f'build/stereo-game-{depth}-{expanded}-{eye}.png')
            different=int(np.any(images[0]!=images[1],axis=2).sum())
            state=exchange();assert not state['failed'] and len(state['fighters'])==2,state
            assert {f['kind'] for f in state['fighters']}=={3,18},state
            if depth:assert different>100,different
            record={'slider':depth,'expanded':expanded,'different_eye_pixels':different,'state':state,'profile':summarize(start,end)}
            records.append(record);print(json.dumps(record),flush=True)
            (ROOT/'build/stereo-gameplay-test.json').write_text(json.dumps(records,indent=2))
        set_word('mp_test_stereo_slider',700)
        subprocess.run([sys.executable,str(ROOT/'tools/cpu_attack_test.py'),'--seconds','120'],check=True)
    finally:
        for name,value in original.items():set_word(name,value)

if __name__=='__main__':main()
