"""Compare unlit identity, general/lit, and mixed CPU-marker vertices in stereo."""
import argparse,hashlib,json,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,TEST_ELF
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',default='build/unlit-affine-qa/gpu')
    ap.add_argument('--resume',type=int);args=ap.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True)
    token=args.resume or ((time.time_ns()&0x7fffffff) or 1)
    (out/'request.json').write_text(json.dumps(dict(token=token,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest()))+'\n')
    if not args.resume:
        set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',1000);set_word('unlit_affine_verify',token)
    deadline=time.monotonic()+180
    while True:
        time.sleep(.5);state=read(('engine_failed','unlit_affine_verify','unlit_affine_verify_page'))
        assert not state['engine_failed'],state
        if not state['unlit_affine_verify']:break
        if time.monotonic()>deadline:raise TimeoutError(('Fixture remains live; resume this token',token,state))
    report=json.loads((SD/'unlit-affine-fixture-report.json').read_text())
    assert report==dict(token=token,pages=16,cases=768,routes=[[256,0],[64,192]]),report
    pages=[];maximum=changed=0
    for page in range(16):
        arrays=[np.fromfile(SD/f'unlit-affine-fixture-{page}-{mode}.bin',dtype=np.uint8).reshape(800,240,4) for mode in range(2)]
        delta=np.abs(arrays[0].astype(int)-arrays[1].astype(int));err=int(delta.max());different=int(np.count_nonzero(delta))
        maximum=max(maximum,err);changed+=different
        coverage=int(np.count_nonzero(np.any(arrays[1][:,:,1:]!=0,axis=2)))
        assert coverage>10000,(page,'Blank fixture',coverage)
        pages.append(dict(page=page,max_error=err,changed_channels=different,covered_pixels=coverage))
        for eye in range(2):
            for mode in range(2):
                image=np.rot90(arrays[mode][eye*400:(eye+1)*400,:,::-1])
                if page in (0,8,15) or different:Image.fromarray(image).save(out/f'page-{page}-eye-{eye}-mode-{mode}.png')
    result=dict(passed=maximum==0,pages=pages,compared_channels=16*800*240*4,changed_channels=changed,
        maximum_error=maximum,fixture=report,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),physical_fps_verified=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='pages'}),flush=True)
    assert result['passed'],'See retained differing images and results'

if __name__=='__main__':main()
