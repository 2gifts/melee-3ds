"""Compare optimized and reference stereo rendering in a paused live match."""
import json,socket,time
import numpy as np
from PIL import Image
import select_test_stage as select
import bottom_screen_test as bottom
from menu_display_pause_test import clock_state
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
from gameplay_test import symbols,packet,receive

def counters():
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        packet(s,'?');receive(s)
        try:
            result={}
            for name,n in [('shader_shortcut_draws',12),('shader_shortcut_vertices',12),
                           ('palette_rows_sent',4),('palette_rows_skipped',4)]:
                if name not in symbols:continue
                packet(s,f'm{symbols[name]:x},{n:x}');raw=bytes.fromhex(receive(s))
                result[name]=[int.from_bytes(raw[i:i+4],'little') for i in range(0,n,4)]
            return result
        finally:packet(s,'c');packet(s,'D');receive(s)

def main():
    bottom.OUT=bottom.ROOT/'build/update15-qa';bottom.OUT.mkdir(exist_ok=True)
    report_path=bottom.OUT/'render-ab.json'
    if report_path.exists():report_path.unlink() # A failed rerun must not retain an old pass.
    assert bottom.snapshot()['scene']==2 and clock_state()['pause_flags']
    frozen=clock_state()['match_frame'];set_word('mp_test_stereo_slider',1000)
    flags=[n for n in ('gpu_pipeline_disable','shader_shortcuts_disable','palette_upload_disable') if n in symbols]
    profiles=[];images={}
    try:
        for iteration in range(3):
            for reference in (1,0):
                for flag in flags:set_word(flag,reference)
                select.act(frames=60);before=counters();start=snapshot(True);time.sleep(2);end=snapshot(False);after=counters()
                assert clock_state()['match_frame']==frozen,'Match advanced during paused comparison'
                profiles.append(dict(reference=bool(reference),iteration=iteration,
                    counters={k:[(a-b)&0xffffffff for a,b in zip(after[k],before[k])] for k in before},**summarize(start,end)))
                if iteration==0:
                    label='reference' if reference else 'optimized'
                    bottom.capture(label);images[reference]=[np.array(Image.open(bottom.OUT/(label+'-top.png')))]
                    assert np.count_nonzero(images[reference][0])>5000,'Captured scene is blank'
                    sd=bottom.ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
                    right=np.fromfile(sd/'engine-right.bgr',dtype=np.uint8).copy()
                    images[reference].append(right)
        differences=[int(np.count_nonzero(a!=b)) for a,b in zip(images[0],images[1])]
        assert differences==[0,0],('Stereo channel differences',differences)
        result=dict(passed=True,flags=flags,stereo_slider=1000,different_channels=differences,profiles=profiles,
            timing_scope='Azahar CPU phases; physical 3DS FPS requires console measurement')
        report_path.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result),flush=True)
    finally:
        for flag in flags:set_word(flag,0)

if __name__=='__main__':main()
