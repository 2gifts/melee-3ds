"""Compare captured portraits and post-clear visible output in both eyes."""
import argparse,hashlib,json,time
from pathlib import Path
import numpy as np
from PIL import Image
from gameplay_test import TEST_ELF
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--resume',type=int);args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);token=args.resume or (time.time_ns()&0x7fffffff) or 1
    if not args.resume:
        set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',1000)
        time.sleep(.2);set_word('results_capture_verify',token)
    deadline=time.monotonic()+180
    while True:
        try:report=json.loads((SD/'results-capture-report.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):report={}
        if report.get('request')==token:break
        if time.monotonic()>deadline:raise TimeoutError(f'Fixture remains owned; resume token {token}')
        time.sleep(.5)
    assert report['omitted_draws']==report['cases']==32,report
    pairs=[]
    for width in (320,400):
        images=[]
        for mode in (0,1):
            name=f'results-capture-{width}-{mode}.bin';raw=(SD/name).read_bytes();(args.output/name).write_bytes(raw)
            img=np.frombuffer(raw,dtype=np.uint8).reshape(800,240,4)[:,:,::-1];images.append(img)
            Image.fromarray(np.rot90(img)).save(args.output/name.replace('.bin','.png'))
        different=int(np.count_nonzero(images[0]!=images[1]));colors=len(np.unique(images[0].reshape(-1,4),axis=0))
        pairs.append(dict(width=width,different_channels=different,distinct_colors=colors))
        assert different==0 and colors>=16,pairs[-1]
        assert np.array_equal(images[0][:400],images[0][400:]),'Visible portrait/clear differs between eyes'
    state=read(('engine_failed','results_capture_verify'));assert not any(state.values()),state
    result=dict(passed=True,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),report=report,pairs=pairs,
                channels_compared=2*800*240*4,scope='Offscreen routing, left-eye capture, both-eye clear and subsequent visible textured draws')
    (args.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)

if __name__=='__main__':main()
