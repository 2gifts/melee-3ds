"""Observe the original title/attract flow without changing game state."""
import argparse,json,subprocess,sys,time
import select_test_stage as select
import bottom_screen_test as bottom
from profile_switch import set_word
from profile_render_detail import snapshot,summarize

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=150)
    ap.add_argument('--label',default='attract');ap.add_argument('--profile',action='store_true')
    ap.add_argument('--validate-packets',action='store_true')
    args=ap.parse_args();bottom.OUT=bottom.ROOT/'build/update14-qa';bottom.OUT.mkdir(exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    if args.validate_packets:set_word('draw_packet_validate',1)
    deadline=time.monotonic()+args.seconds;previous=None;states=[];profiled=False
    try:
        while time.monotonic()<deadline:
            state=bottom.snapshot()
            assert not state['engine_failed'],state
            key=(state['scene'],state['mode'],state['stage'])
            if key!=previous:
                states.append(state);print(json.dumps(state),flush=True);previous=key
            if state['scene']==2 and args.profile and not profiled:
                profiled=True
                bottom.capture(args.label+'-demo')
                start=snapshot(True);time.sleep(2);end=snapshot(False)
                result=summarize(start,end)
                (bottom.OUT/(args.label+'-profile.json')).write_text(json.dumps(result,indent=2)+'\n')
                subprocess.run([sys.executable,str(bottom.ROOT/'tools/profile_cpu_samples.py'),
                    '--samples','150','--label','update14-'+args.label+'-cpu'],check=True)
            time.sleep(.5)
    finally:
        (bottom.OUT/(args.label+'-cycle.json')).write_text(json.dumps(states,indent=2)+'\n')

if __name__=='__main__':main()
