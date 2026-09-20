"""Reach the physical-log encounter using original menus and controller input."""
import argparse,json,sys
import select_test_stage as select
import bottom_screen_test as bottom
from profile_switch import set_word
from texture_visibility_test import read
from gameplay_test import symbols

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--stage',type=int,default=11);ap.add_argument('--output',default='build/feasibility-qa')
    ap.add_argument('--characters',type=int,nargs='+',default=[9,1,22,16])
    ap.add_argument('--rematch',action='store_true',help='Leave an original paused Versus match through L+R+A+Start, then reuse its CSS')
    args=ap.parse_args()
    bottom.OUT=bottom.ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',0)
    for name,value in [('stereo_reuse_disable',1),('gpu_early_queue_disable',0),('gpu_early_queue_bytes',32768),
                       ('gpu_stream_queue_disable',0),('gpu_async_present_disable',0),('mp_render_worker_async',1),
                       ('mp_render_worker_disable',0),('mp_render_worker_geometry_borrow',1),
                       ('mp_render_worker_source_cache_disable',0),('efb_rgb5a3_disable',0),
                       ('efb_discard_disable',0),('unit_attenuation_disable',0),('results_capture_disable',0)]:set_word(name,value)
    # The physical-baseline engine compiles this setting to constant enabled.
    if 'shade_clamped_disable' in symbols:set_word('shade_clamped_disable',0,'big')
    if args.rematch:
        from menu_display_pause_test import clock_state
        assert clock_state()['pause_flags'],'Rematch requires an original paused match'
        select.act(0x1160,4);select.act(frames=90)
        for _ in range(24):
            state=select.observe()
            if 'hand' in state:break
            select.act(0x1000,2);select.act(frames=30)
        else:raise AssertionError(('Original results did not return to CSS',state))
    else:
        for _ in range(90):
            state=select.observe()
            if 'menu' in state:break
            select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2);select.act(frames=25)
        else:raise AssertionError('Main menu did not open')
        state=select.open_versus(select.observe())
    for slot,character in enumerate(args.characters):
        icon=next(x['id'] for x in state['characters'] if x['character_kind']==character)
        state=bottom.choose(state,slot,icon)
    bottom.capture('setup-css');select.act(0x1000,4);state=select.act(frames=50)
    assert 'icons' in state,state
    index=next(x['id'] for x in state['icons'] if x['stage_kind']==args.stage)
    old=sys.argv;sys.argv=['select_test_stage.py',str(index)]
    try:select.main()
    finally:sys.argv=old
    for _ in range(30):
        state=bottom.snapshot()
        if state['scene']==2:break
        assert not state['engine_failed'],state
        select.act(frames=3)
    else:raise AssertionError(('First gameplay frame did not publish',state))
    set_word('mp_test_stereo_slider',1000)
    if not bottom.snapshot()['mp_native_expanded']:bottom.touch(160,225,'mp_native_expanded',1)
    # Match startup observes real original objects; no roster/state injection.
    state=bottom.snapshot();assert state['stage']==args.stage and state['scene']==2,state
    (bottom.OUT/'setup.json').write_text(json.dumps(dict(scene=state,worker=read(('mp_render_worker_active','mp_render_worker_core'))),indent=2))
    bottom.capture('setup-match');print(json.dumps(state),flush=True)

if __name__=='__main__':main()
