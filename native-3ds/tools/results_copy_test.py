"""Original four-player Results: exact conversion validation and balanced A/B.

Results animations continue during the timing windows. No debugger traffic
occurs within them; these remain emulator timings, not physical FPS evidence.
"""
import argparse,hashlib,json,subprocess,sys,time
from gameplay_test import ROOT,TEST_ELF
import bottom_screen_test as bottom
import select_test_stage as select
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
from texture_visibility_test import read
from menu_display_pause_test import clock_state

def setup(out):
    select.observe((0,1,0,0));set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',1000)
    for n in ('gpu_early_queue_disable','gpu_stream_queue_disable','gpu_async_present_disable','efb_discard_disable','efb_rgb5a3_disable'):set_word(n,0)
    set_word('shade_clamped_disable',0,'big');set_word('efb_rgb5a3_validate',1)
    for _ in range(60):
        state=select.observe()
        if 'menu' in state:break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,3);select.act(frames=20)
    else:raise RuntimeError('Main menu did not open')
    state=select.open_versus(state)
    for slot,icon in enumerate((1,6,16,23)):state=bottom.choose(state,slot,icon)
    select.act(0x1000,4);state=select.act(frames=45)
    index=next(row['id'] for row in state['icons'] if row['stage_kind']==31)
    old=sys.argv;sys.argv=['select_test_stage.py',str(index)]
    try:select.main()
    finally:sys.argv=old
    select.act(frames=180);before=bottom.snapshot();assert before['scene']==2 and not before['engine_failed'],before
    select.act(0x1000,3);select.act(frames=20);assert clock_state()['pause_flags']
    subprocess.run([sys.executable,str(ROOT/'tools/results_profile_test.py'),'--output',str(out/'flow')],check=True)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--setup',action='store_true');ap.add_argument('--output',default='build/results-copy-qa');args=ap.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True);bottom.OUT=out
    if args.setup:setup(out)
    assert bottom.snapshot()['scene']==5
    set_word('efb_rgb5a3_validate',1);set_word('efb_discard_disable',0);set_word('efb_rgb5a3_disable',0)
    counts=('engine_frames','engine_failed','efb_discarded_copies','efb_rgb5a3_checks','efb_rgb5a3_pixels','efb_cpu_copies')
    before=read(counts);time.sleep(5);after=read(counts)
    assert not after['engine_failed'] and after['efb_discarded_copies']>before['efb_discarded_copies']+10
    assert after['efb_rgb5a3_checks']>before['efb_rgb5a3_checks']+10
    set_word('efb_rgb5a3_validate',0)
    profiles=[]
    try:
        for mode in (0,1,2,2,1,0):
            set_word('efb_discard_disable',int(mode!=2));set_word('efb_rgb5a3_disable',int(mode==0))
            time.sleep(1)
            c0=read(counts);a=snapshot(True);time.sleep(8);b=snapshot(False);c1=read(counts)
            assert not c1['engine_failed'] and bottom.snapshot()['scene']==5
            delta={k:c1[k]-c0[k] for k in counts};n=delta['engine_frames'];assert n>30
            assert delta['efb_discarded_copies']==0 if mode!=2 else delta['efb_discarded_copies']>n
            assert delta['efb_rgb5a3_checks']==0,'Validation was active during the timing window'
            row=dict(mode=mode,counts=delta,**summarize(a,b));profiles.append(row)
            (out/'progress.json').write_text(json.dumps(profiles,indent=2)+'\n');print(json.dumps(row),flush=True)
    finally:
        set_word('efb_discard_disable',0);set_word('efb_rgb5a3_disable',0);set_word('efb_rgb5a3_validate',0)
    image=bottom.capture('results-optimized')
    report=dict(passed=True,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),validation_before=before,validation_after=after,
        profiles=profiles,image=image,physical_fps_verified=False,scope='Original animated Results, balanced emulator windows with no debugger connections inside them')
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
