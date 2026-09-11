"""Controller-driven item settings and name-menu regression checks."""
import json, socket, time, struct
from select_test_stage import ROOT, observe, act, symbols, packet, receive
import menu_visual_test as visual

def detail():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        packet(sock,'?');receive(sock)
        def read(a,n):
            packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        try:
            flow=read(symbols['mn_804A04F0'],24)
            result={'kind':flow[0],'hovered':int.from_bytes(flow[2:4],'big'),'confirmed':flow[4]}
            if flow[0]==16:
                gobj=int.from_bytes(read(symbols['mnItemSw_804D6BE8'],4),'big')
                data=int.from_bytes(read(gobj+0x2c,4),'big')
                value=read(data,0x24)
                result.update(items=list(value[2:33]),frequency=value[33])
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def capture(label):
    visual.capture('fix3-'+label)
    visual.records[-1]['detail']=detail()

def select(kind,selection):
    for _ in range(40):
        s=act(frames=15)
        if kind==13:
            m=detail();assert m['kind']==kind,m
            if m['hovered']==selection:
                act(0x100);return act(frames=45)
            act(4 if m['hovered']<selection else 8);continue
        if s.get('menu',{}).get('kind')!=kind:raise RuntimeError(s)
        m=s['menu']
        if m['state']!=0:continue
        if m['selection']==selection:
            act(0x100);return act(frames=45)
        act(4 if m['selection']<selection else 8)
    raise TimeoutError('Menu selection')

def main():
    deadline=time.monotonic()+150
    while time.monotonic()<deadline:
        try:s=observe()
        except (ConnectionRefusedError,ConnectionResetError):time.sleep(.1);continue
        assert not s['failed'],s
        if s['frame']>=450 and ('menu' in s or detail()['kind'] in (13,16)):act();break
        time.sleep(.1)
    else:raise TimeoutError('Main menu')
    if detail()['kind']==0:select(0,1);select(2,3)
    if detail()['kind']==13:capture('rules');select(13,5)
    capture('items-initial')
    state=detail();assert state['kind']==16,state
    # From the first item, UP wraps to the frequency row.
    if state['hovered']==0:act(8);act(frames=15);state=detail()
    assert state['hovered'] in (31,32),state
    seen=set()
    for i in range(6):
        state=detail();seen.add(state['frequency']);capture('frequency-'+str(state['frequency']))
        act(1);act(frames=12)
        assert detail()['frequency']==(state['frequency']+1)%6,(state,detail())
    assert seen==set(range(6)),seen
    # Toggle the first item and verify it survives leaving/reopening the menu.
    act(4);act(frames=12);before=detail();assert before['hovered'] in (0,16),before
    act(0x100);act(frames=15);after=detail()
    slot=before['hovered'];assert after['items'][slot]==1-before['items'][slot]
    capture('item-toggled');act(0x200);act(frames=40);select(13,5)
    restored=detail();assert restored['items']==after['items'] and restored['frequency']==after['frequency'],(after,restored)
    capture('items-reopened')
    act(0x200);act(frames=40);act(0x200);act(frames=40)
    select(2,4);capture('names')
    print(json.dumps({'item_frequencies':sorted(seen),'item_settings_persisted':True,'name_menu':detail()}),flush=True)

if __name__=='__main__':
    try:main()
    finally:(ROOT/'build/match-fix-menu-test.json').write_text(json.dumps(visual.records,indent=2)+'\n')
