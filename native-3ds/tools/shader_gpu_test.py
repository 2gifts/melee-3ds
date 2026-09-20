"""Compare real PICA-path pixels for matrix and material specialization."""
import argparse,json,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--flags',type=int,default=1)
    ap.add_argument('--output',default='build/update15-qa');args=ap.parse_args()
    output=ROOT/args.output;output.mkdir(parents=True,exist_ok=True)
    set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',1000);set_word('shader_verify',args.flags)
    deadline=time.monotonic()+60
    while True:
        time.sleep(.25);s=read(('engine_failed','shader_verify'))
        assert not s['engine_failed'],s
        if not s['shader_verify']:break
        if time.monotonic()>deadline:raise TimeoutError(s)
    a=np.fromfile(SD/'shader-fixture-0.bin',dtype=np.uint8).reshape(800,240,4)
    b=np.fromfile(SD/'shader-fixture-1.bin',dtype=np.uint8).reshape(800,240,4)
    delta=np.abs(a.astype(int)-b.astype(int))
    # Coverage must match exactly. A single output-channel rounding step is
    # permitted for the short constant-color program on PICA float24.
    assert np.array_equal(np.any(a[:,:,1:]!=0,axis=2),np.any(b[:,:,1:]!=0,axis=2))
    assert delta.max()<=1,('Shader channel difference',int(delta.max()))
    counts=[]
    for eye in range(2):
        image=np.rot90(b[eye*400:(eye+1)*400,:,::-1]);Image.fromarray(image).save(output/f'shader-{eye}.png')
        for row in range(2):
            for col in range(4):
                region=image[(25 if row==0 else 125):(100 if row==0 else 215),int(20+90*col):int(85+90*col),:3]
                counts.append(int(np.count_nonzero(np.any(region,axis=2))))
    assert min(counts)>100,('Blank shader case',counts)
    routes=json.loads((SD/'shader-route-report.json').read_text())
    assert routes['reference']==[0,0,8]
    assert routes['candidate']==([0,0,8] if args.flags&4 else [2,2,4]),routes
    if not args.flags&8:assert routes['uniform_checks'][1]<routes['uniform_checks'][0],routes
    result=dict(passed=True,max_channel_error=int(delta.max()),changed_channels=int(np.count_nonzero(delta)),actual_shader_routes=routes,
                compared_channels=int(delta.size),case_coverage=counts,eyes=2,cases=8)
    (output/'shader-gpu.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':main()
