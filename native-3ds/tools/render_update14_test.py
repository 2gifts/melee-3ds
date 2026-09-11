"""A/B new render paths in one paused scene, plus exact live rotations."""
import json,socket,time
from pathlib import Path
import numpy as np
from PIL import Image
import select_test_stage as select
import bottom_screen_test as bottom
from menu_display_pause_test import clock_state
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
from gameplay_test import symbols,packet,receive


def counters():
    names=('texture_bind_hits','texture_bind_updates','draw_packet_draws','draw_packet_checks',
           'mp_rotation_hits','mp_rotation_misses','mp_rotation_zeros','mp_rotation_checks')
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        packet(sock,'?');receive(sock)
        try:
            result={}
            for name in names:
                packet(sock,f'm{symbols[name]:x},4')
                result[name]=int.from_bytes(bytes.fromhex(receive(sock)),
                    'big' if name.startswith('mp_rotation') else 'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)


def main():
    bottom.OUT=bottom.ROOT/'build/update14-qa';bottom.OUT.mkdir(exist_ok=True)
    assert bottom.snapshot()['scene']==2
    set_word('mp_test_stereo_slider',1000)
    before=counters();set_word('mp_rotation_cache_validate',1,'big');time.sleep(3);after=counters()
    assert after['mp_rotation_checks']>before['mp_rotation_checks']+1000,(before,after)
    set_word('mp_rotation_cache_validate',0,'big');set_word('draw_packet_validate',0)
    if not clock_state()['pause_flags']:
        select.act(0x1000,3);select.act(frames=60)
    frozen=clock_state();assert frozen['pause_flags'],frozen
    results=[];images={}
    try:
        for iteration in range(3):
            for reference in (1,0):
                set_word('texture_bind_disable',reference);set_word('draw_packet_disable',reference)
                select.act(frames=90)
                first=counters();start=snapshot(True);time.sleep(3);end=snapshot(False);last=counters()
                assert clock_state()['match_frame']==frozen['match_frame']
                result=summarize(start,end)
                result.update(reference=bool(reference),iteration=iteration,
                              counters={k:(last[k]-first[k])&0xffffffff for k in first})
                results.append(result)
                if iteration==0:
                    label='render-reference' if reference else 'render-cached'
                    bottom.capture(label);images[reference]=np.array(Image.open(bottom.OUT/(label+'-top.png')))
        diff=int(np.count_nonzero(np.any(images[0]!=images[1],axis=2)))
        # Paused game actors remain fixed; the screenshot also checks the
        # complete scene independently of the synthetic GPU fixtures.
        assert diff==0,('Paused scene pixel mismatch',diff)
        report={'passed':True,'different_pixels':diff,'stereo_slider':1000,
                'rotation_validation':{'before':before,'after':after},'profiles':results,
                'timing_scope':'Azahar CPU phase comparison, not physical 3DS FPS'}
        (bottom.OUT/'render-ab.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report),flush=True)
    finally:
        set_word('texture_bind_disable',0);set_word('draw_packet_disable',0)
        if clock_state()['pause_flags']:select.act(0x1000,3);select.act(frames=12)


if __name__=='__main__':main()
