"""Check perspective parallax, convergence, slider strength and flat HUD pixels."""
import json,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read

def main():
    original=read(('mp_test_stereo_slider',))
    results=[]
    try:
        set_word('mp_test_stereo_slider',1000);set_word('stereo_verify',1)
        deadline=time.monotonic()+90
        while True:
            time.sleep(.5);s=read(('engine_failed','stereo_verify','stereo_active'))
            assert not s['engine_failed'],s
            if not s['stereo_verify']:break
            if time.monotonic()>deadline:raise TimeoutError(s)
        for mode in range(4):
            assert (SD/f'stereo-fixture-{mode}.bin').read_bytes()==(SD/f'stereo-fixture-{mode+4}.bin').read_bytes(),('eye order pixel mismatch',mode)
            width=320 if mode<2 else 400;strength=.5 if mode&1 else 1
            pixels=np.fromfile(SD/f'stereo-fixture-{mode}.bin',dtype=np.uint8).reshape(800,240,4)[:,:,::-1]
            pair=[np.rot90(pixels[i*400:(i+1)*400]) for i in range(2)]
            for eye,im in enumerate(pair):Image.fromarray(im).save(ROOT/f'build/stereo-fixture-{mode}-{eye}.png')
            checks=[]
            for item,color in enumerate(((255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,255,255),(255,0,255,255))):
                centers=[]
                for eye,im in enumerate(pair):
                    yy,xx=np.where(np.all(im==color,axis=2));assert len(xx)>200,(mode,item,eye,len(xx))
                    centers.append(float(xx.mean()))
                expected=(12,0,-6,0,6)[item]*strength
                disparity=centers[0]-centers[1]
                assert abs(disparity-expected)<=.6001,(mode,item,centers,expected)
                checks.append({'item':item,'centers':centers,'disparity':disparity,'expected':expected})
            if width==320:
                for im in pair:assert not im[:,:40,:3].any() and not im[:,360:,:3].any()
            results.append({'width':width,'slider':strength,'checks':checks})
        (ROOT/'build/stereo-gpu-test.json').write_text(json.dumps(results,indent=2)+'\n')
        print('Stereo GPU pixels passed: 4 modes, 20 depth/HUD/ribbon comparisons, 768000 exact eye-order pixels',flush=True)
    finally:
        set_word('stereo_verify',0)
        for name,value in original.items():set_word(name,value)

if __name__=='__main__':main()
