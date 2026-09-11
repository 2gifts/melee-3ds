"""Exercise Event Match asset loading, page changes and return via real input."""
import json,socket,struct
import select_test_stage as select
import bottom_screen_test as bottom
from gameplay_test import symbols,packet,receive
from profile_switch import set_word
from menu_refinement_test import enter,hover,idle

def event_state():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        def word(a):return int.from_bytes(read(a,4),'big')
        try:
            assert read(symbols['engine_failed'],4)==b'\0'*4
            assert read(symbols['mn_804A04F0'],1)==b'\x07'
            gobj=word(symbols['mnEvent_804D6C60']);assert gobj
            data=read(word(gobj+0x2c),0x7c)
            first=int.from_bytes(data[4:8],'big');page=data[0]
            models=struct.unpack('>9I',data[8:44]);texts=struct.unpack('>9I',data[44:80])
            # Trophy models exist only for completed events; a fresh offline
            # save still has all nine event labels and names.
            assert all(texts) and all(struct.unpack('>9I',data[80:116])) and 0<=page<9 and 0<=first<=50
            assert all(struct.unpack('>2I',data[0x74:0x7c]))
            return {'first_event':first,'page':page,'selected_event':first+page,'rows':len(models)}
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    bottom.OUT=bottom.ROOT/'build/update13-qa';bottom.OUT.mkdir(exist_ok=True)
    set_word('mp_test_frame_limit',0);select.observe((0,1,0,0))
    for _ in range(70):
        if 'menu' in select.observe():break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2);select.act(frames=20)
    enter(0,1);records=[]
    for cycle in range(3):
        hover(1);select.act(0x100,3);select.act(frames=100)
        records.append(event_state());bottom.capture(f'event-enter-{cycle}')
        for button in [4]*10+[0x400]*6+[0x800]*6+[8]*9:
            select.act(button,3);select.act(frames=12);records.append(event_state())
        assert records[-1]['selected_event']==0,records[-1]
        select.act(0x200,3);select.act(frames=100);idle(1)
    (bottom.OUT/'event-menu-test.json').write_text(json.dumps({'passed':True,'cycles':3,'observations':records},indent=2)+'\n',encoding='utf-8')
    print('Event menu: three entries, scrolling/page changes and exits passed.',flush=True)

if __name__=='__main__':main()
