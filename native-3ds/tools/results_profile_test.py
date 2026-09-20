"""Reach original four-player Results from an owned, paused development match.

The fixture shortens the remaining timer to five seconds; original timeout,
standings, animations and Results code then run normally. This is a test-only
state edit, not evidence for controller-only play or hardware FPS.
"""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,TEST_ELF,symbols,packet,receive
import bottom_screen_test as bottom
import select_test_stage as select
from menu_display_pause_test import clock_state
from profile_render_detail import snapshot,summarize

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',default='build/update19-results-profile');args=ap.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True)
    before=bottom.snapshot();clock=clock_state()
    assert before['scene']==2 and clock['pause_flags'] and sum(p['kind']!=3 for p in before['players'])==4
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            packet(sock,f'M{symbols["controller"]+0x28:x},6:'+struct.pack('>IH',5,0).hex());assert receive(sock)=='OK'
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    select.act(0x1000,3);select.act(frames=20)
    assert not clock_state()['pause_flags']
    deadline=time.monotonic()+180
    while bottom.snapshot()['scene']!=5:
        assert not bottom.snapshot()['engine_failed']
        if time.monotonic()>deadline:raise TimeoutError('Original timeout did not reach Results')
        time.sleep(1)
    select.act(frames=240)
    bottom.OUT=out;image=bottom.capture('four-player-results')
    start=snapshot(True)
    try:time.sleep(12)
    finally:end=snapshot(False)
    result=dict(fixture='Remaining timer shortened to five seconds, original timeout then ran',elf=str(TEST_ELF),before=before,clock=clock,results=image,profile=summarize(start,end))
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True)
if __name__=='__main__':main()
