"""Compare both-eye GPU output against the separately compiled old shader."""
import argparse,hashlib,json,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,TEST_ELF,symbols
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',default='build/update17-qa/lighting')
    ap.add_argument('--unit-attenuation',action='store_true',help='Compare constant-unit attenuation predicates against the same full shader')
    ap.add_argument('--resume',type=int,help='Resume observation of this existing fixture token without restarting it')
    args=ap.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True)
    token=args.resume or ((time.time_ns()&0x7fffffff) or 1)
    (out/'request.json').write_text(json.dumps(dict(token=token,resumed=bool(args.resume)))+'\n')
    if not args.resume:
        set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',1000)
        if 'lighting_unit_verify' in symbols:set_word('lighting_unit_verify',int(args.unit_attenuation))
        set_word('lighting_verify',token)
    names=('engine_failed','lighting_verify')+(('lighting_verify_page',) if 'lighting_verify_page' in symbols else ())
    deadline=time.monotonic()+180;last=None
    while True:
        time.sleep(.5);state=read(names)
        (out/'progress.json').write_text(json.dumps(state)+'\n')
        assert not state['engine_failed'],state
        if state['lighting_verify']==0:break
        if state.get('lighting_verify_page')!=last:
            last=state['lighting_verify_page'];print('GPU page',last,flush=True)
        if time.monotonic()>deadline:raise TimeoutError(('Fixture still active; emulator preserved',state))
    prefix='unit-' if args.unit_attenuation else ''
    report=json.loads((SD/(prefix+'lighting-fixture-report.json')).read_text())
    unit_skips=report.pop('unit_vertex_evaluations_skipped',0)
    if args.unit_attenuation:assert unit_skips>5000,unit_skips
    assert report==dict(token=token,pages=61,cases=2916,draws=[2916,2916]),report
    pages=[];count=0;changed=0;maximum=0
    for page in range(61):
        paths=[SD/f'{prefix}lighting-fixture-{page}-{mode}.bin' for mode in range(2)]
        frames=[np.fromfile(p,dtype=np.uint8).reshape(800,240,4) for p in paths]
        delta=np.abs(frames[0].astype(int)-frames[1].astype(int))
        error=int(delta.max());different=int(np.count_nonzero(delta))
        maximum=max(maximum,error);changed+=different;count+=delta.size
        assert error==0,('Pixel difference',page,error,different)
        cells=min(48,2916-page*48);coverage=[]
        for eye in range(2):
            image=np.rot90(frames[1][eye*400:(eye+1)*400,:,::-1])
            for cell in range(cells):
                # Quad positions map to 49x38.4-pixel cells; test an interior
                # patch away from all edges and from the stereo displacement.
                x=int(200+(-.94+.245*(cell%8)+.085)*200)
                y=int(120-(-.91+.32*(cell//8)+.115)*120)
                pixels=int(np.count_nonzero(np.any(image[y-4:y+4,x-4:x+4,:3],axis=2)))
                coverage.append(pixels)
            if page in (0,30,60):Image.fromarray(image).save(out/f'page-{page}-eye-{eye}.png')
        assert min(coverage)>32,('Blank lighting case',page,coverage)
        pages.append(dict(page=page,changed_channels=different,min_case_coverage=min(coverage),
                          sha256=[hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]))
    result=dict(passed=True,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),cases=2916,eyes=2,compared_channels=count,changed_channels=changed,unit_vertex_evaluations_skipped=unit_skips,
                maximum_error=maximum,pages=pages,fixture=report,physical_fps_verified=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='pages'}),flush=True)
if __name__=='__main__':main()
