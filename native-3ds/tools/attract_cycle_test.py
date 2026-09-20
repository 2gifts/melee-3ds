"""Observe the original title/attract flow without changing game state."""
import argparse,json,subprocess,sys,time
import select_test_stage as select
import bottom_screen_test as bottom
from profile_switch import set_word
from profile_render_detail import snapshot,summarize

def coverage(states):
    battles=returns=four_player=0;in_battle=False;pending_return=False
    for state in states:
        battle=state['scene']==2 and state['mode']==24
        if battle and not in_battle:
            battles+=1
            four_player+=sum(p['kind']!=3 for p in state['players'])==4
            pending_return=True
        # The original flow may play a movie between a demo and the title.
        if pending_return and state['scene']==0:returns+=1;pending_return=False
        in_battle=battle
    return dict(battles=battles,title_returns=returns,four_player_battles=four_player)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=150)
    ap.add_argument('--label',default='attract');ap.add_argument('--profile',action='store_true')
    ap.add_argument('--validate-packets',action='store_true')
    ap.add_argument('--output',default='build/update15-qa')
    ap.add_argument('--min-battles',type=int,default=1)
    ap.add_argument('--min-four-player',type=int,default=0)
    args=ap.parse_args();bottom.OUT=bottom.ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    if args.validate_packets:set_word('draw_packet_validate',1)
    deadline=time.monotonic()+args.seconds;previous=None;states=[];profiled=False
    progress=time.monotonic();last_frame=None;completed=False
    try:
        while time.monotonic()<deadline:
            state=bottom.snapshot()
            assert not state['engine_failed'],state
            if state['engine_frames']!=last_frame:
                progress=time.monotonic();last_frame=state['engine_frames']
            assert time.monotonic()-progress<15,('No frame progress for 15 seconds',state)
            key=(state['scene'],state['mode'],state['stage'])
            if key!=previous:
                states.append(state);print(json.dumps(state),flush=True);previous=key
            if state['scene']==2 and state['mode']==24 and args.profile and not profiled:
                profiled=True
                bottom.capture(args.label+'-demo')
                start=snapshot(True);time.sleep(2);end=snapshot(False)
                result=summarize(start,end)
                (bottom.OUT/(args.label+'-profile.json')).write_text(json.dumps(result,indent=2)+'\n')
                subprocess.run([sys.executable,str(bottom.ROOT/'tools/profile_cpu_samples.py'),
                    '--samples','150','--label','update15-'+args.label+'-cpu'],check=True)
            time.sleep(.5)
        result=coverage(states)
        assert result['battles']>=args.min_battles and result['title_returns']>=1 and result['four_player_battles']>=args.min_four_player, ('Missing actual attract battle/return coverage',result)
        completed=True
    finally:
        (bottom.OUT/(args.label+'-cycle.json')).write_text(json.dumps(states,indent=2)+'\n')
        (bottom.OUT/(args.label+'-cycle-status.json')).write_text(json.dumps(dict(
            passed=completed,seconds=args.seconds,transitions=len(states),
            **coverage(states),last_frame=last_frame),indent=2)+'\n')
        if args.validate_packets:set_word('draw_packet_validate',0)
        set_word('mp_gx_profile',0,'big')

if __name__=='__main__':main()
