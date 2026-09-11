"""Launch an original Classic encounter using its existing debug round setting.

Only the six-byte Classic setup's starting round is injected on CSS. Assets,
match initialization, trophy selection, timer, hits and scene transitions run
through the original game. Emulator timings are never hardware FPS claims.
"""
import argparse,json,socket,subprocess,sys,time
import select_test_stage as select
import bottom_screen_test as bottom
from gameplay_test import symbols,packet,receive
from profile_switch import set_word
from menu_refinement_test import enter
from profile_render_detail import snapshot,summarize

def launch(round_number,stereo=0,intro_label=None):
    set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',stereo)
    select.observe((0,1,0,0))
    for _ in range(70):
        state=select.observe()
        if 'menu' in state:break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2)
        select.act(frames=20)
    else:raise AssertionError(state)
    enter(0,1);enter(0,6);select.act(0x100,3)
    for _ in range(70):
        state=select.observe()
        if 'hand' in state:break
        select.act(frames=15)
    else:raise AssertionError(state)
    bottom.choose(state,0,1)
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            packet(sock,f'm{symbols["gmMainLib_804D3EE0"]:x},4')
            base=int.from_bytes(bytes.fromhex(receive(sock)),'big')
            packet(sock,f'M{base+0x521:x},1:{round_number:02x}');assert receive(sock)=='OK'
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    select.observe((0x1000,4,0,0));deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        state=bottom.snapshot();assert not state['engine_failed'],state
        if state['scene']==2:
            select.observe((0,1,0,0));return state
        if state['scene']==32:
            if intro_label:
                select.observe((0,1,0,0))
                bottom.capture(intro_label+'-intro');intro_label=None
            select.observe((0x1000,2,0,0))
        time.sleep(.05)
    raise AssertionError(('Classic round did not load',round_number,state))

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--round',type=int,default=5,choices=range(11))
    ap.add_argument('--stereo',type=int,default=0,choices=(0,1000))
    ap.add_argument('--seconds',type=int,default=4);ap.add_argument('--label',default='classic')
    ap.add_argument('--complete-bonus',action='store_true')
    ap.add_argument('--compare-geometry',action='store_true')
    ap.add_argument('--validate-geometry',action='store_true')
    ap.add_argument('--capture-intro',action='store_true');args=ap.parse_args()
    bottom.OUT=bottom.ROOT/'build/update13-qa';bottom.OUT.mkdir(exist_ok=True)
    launch(args.round,args.stereo,args.label if args.capture_intro else None);select.act(frames=100)
    initial=bottom.capture(args.label+'-loaded');assert initial['scene']==2
    start=snapshot(True)
    try:time.sleep(args.seconds)
    finally:end=snapshot(False)
    profile=summarize(start,end);assert profile['same_scene'],profile
    result={'round':args.round,'stereo':args.stereo,'initial':initial,'profile':profile}
    if args.validate_geometry:
        from gameplay_test import exchange
        before=exchange();assert bottom.snapshot()['scene']==2
        subprocess.run([sys.executable,str(bottom.ROOT/'tools/native_geometry_test.py'),'--seconds','5'],check=True)
        after=exchange();assert bottom.snapshot()['scene']==2
        assert before['fighters']!=after['fighters'],('Fighters did not animate',before,after)
        result['animated_geometry']={'before':before,'after':after,
            'checks':json.loads((bottom.ROOT/'build/native-geometry-test.json').read_text())}
    if args.compare_geometry:
        from menu_display_pause_test import clock_state
        bottom.OUT=bottom.ROOT/'build/update13-qa'
        select.act(0x1000,3);select.act(frames=20)
        paused=clock_state();assert paused['pause_flags']&2,paused
        comparisons=[]
        try:
            for sharing in (0,1,0,1):
                set_word('geometry_range_share',sharing,'big');select.act(frames=80)
                start=snapshot(True)
                try:time.sleep(3)
                finally:end=snapshot(False)
                p=summarize(start,end);assert p['same_scene'],p
                comparisons.append({'sharing':sharing,**p})
            after=clock_state()
            assert paused['match_frame']==after['match_frame'],(paused,after)
        finally:set_word('geometry_range_share',1,'big')
        result['paused_renderer_comparison']=comparisons
        select.act(0x1000,3);select.act(frames=40)
        assert not clock_state()['pause_flags']
    if args.complete_bonus:
        assert args.round in (2,5,8)
        # No forced victory: let the original timer expire and its result flow run.
        deadline=time.monotonic()+160;seen_result=False
        while time.monotonic()<deadline:
            state=bottom.snapshot();assert not state['engine_failed'],state
            if state['scene']!=2:seen_result=True
            if seen_result and state['scene']==2:break
            time.sleep(.15)
        else:raise AssertionError(('Bonus did not exit through its result flow',state))
        result['next_encounter']=bottom.capture(args.label+'-next')
    (bottom.OUT/(args.label+'.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2),flush=True)

if __name__=='__main__':main()
