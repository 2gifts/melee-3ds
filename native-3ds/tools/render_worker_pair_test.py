"""Compare full stereo images across native CPU ownership modes.

Uses an already paused original match; timing is deliberately not measured.
"""
import argparse,hashlib,json
import numpy as np
from PIL import Image
import bottom_screen_test as bottom
import select_test_stage as select
from gameplay_test import ROOT,TEST_ELF,symbols
from menu_display_pause_test import clock_state
from profile_switch import set_word
from texture_visibility_test import read
from efb_copy_test import SD

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);args=ap.parse_args()
    bottom.OUT=ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    initial=clock_state();assert initial['pause_flags'],initial
    initial_state=bottom.snapshot();assert initial_state['scene']==2 and not initial_state['engine_failed']
    names=('mp_render_worker_jobs','mp_render_worker_snapshots','mp_render_worker_failures',
           'mp_render_worker_source_hits','mp_render_worker_source_checks','mp_render_worker_source_mismatches')
    if 'mp_render_worker_borrowed_draws' in symbols:names+=('mp_render_worker_borrowed_draws','mp_render_worker_geometry_retirements')
    saved=read(('mp_render_worker_disable','mp_render_worker_source_cache_validate'))
    images={};evidence=[]
    try:
        set_word('mp_render_worker_source_cache_validate',1)
        for disabled,label in ((1,'original'),(0,'queued')):
            set_word('mp_render_worker_disable',disabled);select.act(frames=15)
            before=read(names);bottom.capture(label);after=read(names)
            assert clock_state()==initial,'Original paused clock changed'
            assert not bottom.snapshot()['engine_failed'] and not after['mp_render_worker_source_mismatches'],after
            assert not after['mp_render_worker_failures'],after
            delta={k:after[k]-before[k] for k in names}
            assert delta['mp_render_worker_jobs']==0 if disabled else delta['mp_render_worker_jobs']>100,delta
            if not disabled:assert delta['mp_render_worker_snapshots']>100 and delta['mp_render_worker_source_checks']>100,delta
            left=np.array(Image.open(bottom.OUT/(label+'-top.png')))
            right=np.rot90(np.fromfile(SD/'engine-right.bgr',dtype=np.uint8).reshape(400,240,3)[:,:,::-1]).copy()
            Image.fromarray(right).save(bottom.OUT/(label+'-right.png'))
            assert left.shape==right.shape==(240,400,3)
            assert np.count_nonzero(left)>5000 and np.count_nonzero(right)>5000
            assert np.count_nonzero(left!=right)>200,'Eye images unexpectedly identical'
            images[label]=(left,right);evidence.append(dict(mode=label,delta=delta))
        differences=[int(np.count_nonzero(a!=b)) for a,b in zip(images['original'],images['queued'])]
        result=dict(passed=differences==[0,0],different_channels=differences,initial_clock=initial,initial_state=initial_state,
                    modes=evidence,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
                    physical_fps_verified=False,scope='Exact full-resolution stereo in one paused original encounter; no timing claim')
        (bottom.OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
        assert result['passed'],differences
    finally:
        for name,value in saved.items():set_word(name,value)

if __name__=='__main__':main()
