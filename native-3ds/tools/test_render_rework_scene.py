"""Paired rendering and byte validation in an original frozen encounter."""
import argparse,json,time,hashlib
import numpy as np
import bottom_screen_test as bottom
from gameplay_test import ROOT,TEST_ELF,symbols
from results_capture_scene_test import connection,frozen_processes,capture
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
import select_test_stage as select

def counters():
    with connection() as (get,put):
        return {n:int.from_bytes(get(symbols[n],4),e) for n,e in [('engine_frames','little'),('engine_failed','little'),('gpu_clamped_vertices','big'),('clamped_native_vertices','little'),('geometry_checks','big'),('geometry_compare_checks','big'),('geometry_dirty_ranges','big'),('geometry_dirty_reuses','big'),('geometry_dirty_checks','big'),('geometry_early_attempts','big'),('geometry_early_rejected','big'),('geometry_early_checks','big'),('geometry_planes_checks','big'),('audio_decoder_checks','big'),('audio_decoder_fast_ticks','big'),('audio_decoder_reference_ticks','big')] if n in symbols}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);ap.add_argument('--timing',action='store_true');ap.add_argument('--clamped-only',action='store_true');a=ap.parse_args()
    flags=['gpu_clamped_disable'] if a.clamped_only else ['gpu_clamped_disable','geometry_planes_disable','geometry_early_disable']
    def mode(value):
        for name in flags:set_word(name,value,'big')
    bottom.OUT=ROOT/a.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    report={'elf_sha256':hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),'hardware_fps_verified':False}
    try:
        with frozen_processes() as count:
            report['frozen_processes']=count
            mode(1);select.act(frames=8)
            ref=capture('reference');repeat=capture('reference-repeat')
            report['reference_repeat']=[int(np.count_nonzero(x!=y)) for x,y in zip(ref,repeat)]
            assert report['reference_repeat']==[0,0],report
            c0=counters();mode(0);select.act(frames=12)
            candidate=capture('candidate');c1=counters()
            report['counts']={n:(c1[n]-v)&0xffffffff for n,v in c0.items()}
            report['eyes']=[]
            for x,y in zip(ref,candidate):
                delta=np.abs(x.astype(int)-y.astype(int));report['eyes'].append({'changed_pixels':int(np.count_nonzero(np.any(delta,axis=2))),'max_channel_error':int(delta.max()),'mean_channel_error':float(delta.mean()),'pixels_error_gt_8':int(np.count_nonzero(np.max(delta,axis=2)>8))})
            (bottom.OUT/'paired.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
            assert all(eye['max_channel_error']<=1 for eye in report['eyes']),report['eyes']
            set_word('geometry_compare_validate',1,'big');set_word('geometry_validate',1,'big');select.act(frames=10)
            report['validation']=counters();assert not report['validation']['engine_failed']
            set_word('geometry_compare_validate',0,'big');set_word('geometry_validate',0,'big')
            for name in ('geometry_early_validate','geometry_planes_validate'):
                if name in symbols:set_word(name,1,'big')
            select.act(frames=12);report['additional_validation']=counters()
            assert not report['additional_validation']['engine_failed'],report
            for name in ('geometry_early_validate','geometry_planes_validate'):
                if name in symbols:set_word(name,0,'big')
            if a.timing:
                rows=[]
                for disabled in (1,0,0,1):
                    mode(disabled);time.sleep(.5)
                    before=counters();start=snapshot(True);time.sleep(6);end=snapshot(False);after=counters()
                    row={'disabled':disabled,'counts':{n:(after[n]-v)&0xffffffff for n,v in before.items()},**summarize(start,end)}
                    rows.append(row);(bottom.OUT/'timing.json').write_text(json.dumps(rows,indent=2));print(json.dumps(row),flush=True)
        report['passed']=True
        (bottom.OUT/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    finally:
        for name in flags+['geometry_compare_validate','geometry_validate','geometry_early_validate','geometry_planes_validate']:
            if name in symbols:set_word(name,0,'big')
if __name__=='__main__':main()
