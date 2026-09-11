"""Verify menu projection, CSS selection and default pause with real input."""
import argparse,json,socket,struct,sys,time
import select_test_stage as select
import bottom_screen_test as bottom
from gameplay_test import symbols,packet,receive
from profile_switch import set_word

bottom.OUT=bottom.ROOT/'build/update12-qa'

def clock_state():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        try:
            c=read(symbols['controller'],48)
            return {'pause_flags':int.from_bytes(read(symbols['gm_80479D58']+16,1),'big'),
                    'match_frame':int.from_bytes(c[36:40],'big'),'seconds':int.from_bytes(c[40:44],'big'),
                    'subseconds':int.from_bytes(c[44:46],'big'),'pauser':c[1]}
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--fresh',action='store_true')
    ap.add_argument('--output',default='build/update12-qa');args=ap.parse_args()
    set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',0);select.observe((0,1,0,0))
    if args.fresh:
        for _ in range(60):
            state=select.observe()
            if 'menu' in state:break
            select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2);select.act(frames=20)
        else:raise AssertionError(state)
    from menu_refinement_test import enter,back
    bottom.OUT=bottom.ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    enter(0,1);bottom.capture('1p-final');back(0);bottom.capture('main-final')
    state=select.open_versus(select.observe())
    for slot,icon in [(0,1),(1,2)]:state=bottom.choose(state,slot,icon)
    bottom.capture('css-4x3')
    bottom.touch(160,225,'mp_native_expanded',1);select.act(frames=4);bottom.capture('css-wide')
    set_word('mp_test_stereo_slider',1000);select.act(frames=4);bottom.capture('css-wide-stereo')
    select.act(0x1000,4);select.act(frames=40);bottom.capture('stages-wide-stereo')
    old=sys.argv;sys.argv=['select_test_stage.py','25']
    try:select.main()
    finally:sys.argv=old
    select.act(frames=180);bottom.capture('match-before-pause')
    test_pause()

def test_pause():
    assert bottom.snapshot()['scene']==2,'An active Versus match is required'
    records=[]
    for attempt in range(3):
        before=clock_state();assert before['pause_flags']==0,before
        select.act(0x1000,3);select.act(frames=20);paused=clock_state()
        assert paused['pause_flags'] and paused['pauser']==0,paused
        select.act(frames=120);after=clock_state()
        assert [paused[k] for k in ('match_frame','seconds','subseconds')]==[after[k] for k in ('match_frame','seconds','subseconds')],(paused,after)
        if attempt==0:bottom.capture('match-paused')
        select.act(0x1000,3);select.act(frames=120);resumed=clock_state()
        assert not resumed['pause_flags'] and resumed['match_frame']>after['match_frame'],(after,resumed)
        records.append({'before':before,'paused':paused,'after':after,'resumed':resumed})
    bottom.capture('match-resumed')
    (bottom.OUT/'pause-test.json').write_text(json.dumps({'passed':True,'cycles':records},indent=2))
    print('Menu widescreen, CSS input and three default Start pause/resume cycles passed.',flush=True)

if __name__=='__main__':main()
