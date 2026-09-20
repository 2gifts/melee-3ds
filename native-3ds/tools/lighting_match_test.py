"""Observe lighting paths in an evolving casual stereo match; not an FPS oracle."""
import argparse,json,statistics,time
import bottom_screen_test as bottom
import select_test_stage as select
from gameplay_test import ROOT
from menu_display_pause_test import clock_state
from profile_render_detail import snapshot,summarize
from profile_switch import set_word
from render_update16_test import counters

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',default='build/update17-qa/unpaused')
    args=ap.parse_args();out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True);bottom.OUT=out
    initial=bottom.snapshot();stage=initial['stage'];assert initial['scene']==2 and initial['mode']==2
    assert clock_state()['pause_flags'];select.act(0x1000,3);select.act(frames=30)
    profiles=[];passed=False
    try:
        for iteration in range(3):
            for reference in (1,0):
                set_word('lighting_uniform_disable',reference);time.sleep(1)
                before_state=bottom.snapshot();before_clock=clock_state()
                assert before_state['scene']==2 and before_state['stage']==stage and not before_state['engine_failed']
                assert not before_clock['pause_flags'] and sum(p['kind']!=3 for p in before_state['players'])==4
                before=counters();start=snapshot(True);time.sleep(5);end=snapshot(False);after=counters()
                after_state=bottom.snapshot();after_clock=clock_state()
                assert after_state['scene']==2 and after_state['stage']==stage and not after_state['engine_failed']
                assert not after_clock['pause_flags'] and after_clock['match_frame']>before_clock['match_frame']
                delta={k:[(a-b)&0xffffffff for a,b in zip(after[k],before[k])] for k in before}
                draws=delta['lighting_uniform_draws'];assert draws[reference]>100 and draws[1-reference]==0,draws
                result=dict(iteration=iteration,reference=bool(reference),before=before_state,after=after_state,
                            before_clock=before_clock,after_clock=after_clock,counters=delta,**summarize(start,end))
                profiles.append(result)
                (out/'progress.json').write_text(json.dumps(profiles,indent=2)+'\n')
                print(json.dumps(dict(iteration=iteration,reference=reference,renders=result['rendered_frames'],
                                      uniform_ms=result['phases']['uniform_compare']['estimated_ms_per_render'],
                                      uniform_call_us=result['phases']['uniform_compare']['mean_call_us'],
                                      native_draws=result['native_geometry_work_per_render']['requests'],
                                      rates=result['rates'])),flush=True)
        passed=True
    finally:
        set_word('lighting_uniform_disable',0);set_word('mp_gx_profile',0,'big')
        report=dict(passed=passed,initial=initial,profiles=profiles,physical_fps_verified=False,
                    scope='Evolving four-player match; alternating shaders, not identical workloads. Pixel/enum fixtures separately check fidelity.')
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        state=bottom.snapshot()
        if not state['engine_failed'] and state['scene']==2 and state['players'][0]['stocks']>0:
            if not clock_state()['pause_flags']:select.act(0x1000,3);select.act(frames=20)
            assert clock_state()['pause_flags']
    print('Unpaused lighting observation passed; encounter preserved.',flush=True)
if __name__=='__main__':main()
