"""Authoring capture: configure a Fox ditto demo, export its real draw data."""
import json,socket,time,sys
from pathlib import Path
from gameplay_test import ROOT,symbols,packet,receive,exchange
from profile_switch import set_word
import select_test_stage as select
import bottom_screen_test as bottom

def main():
    out=ROOT/'build/home-menu/capture';out.mkdir(parents=True,exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',0)
    for _ in range(90):
        state=select.observe()
        if 'menu' in state:break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2)
        select.act(frames=25)
    else:raise RuntimeError('Main menu did not load')
    state=select.open_versus(state)
    fox_icon=next(row['id'] for row in state['characters'] if row['character_kind']==2)
    for slot in range(2):state=bottom.choose(state,slot,fox_icon)
    select.act(0x1000,4);state=select.act(frames=40)
    index=next(row['id'] for row in state['icons'] if row['stage_kind']==32)
    saved=sys.argv;sys.argv=['select_test_stage.py',str(index)]
    try:select.main()
    finally:sys.argv=saved
    select.act(frames=130)
    assert bottom.snapshot()['scene']==2
    records=[]
    sd=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
    for i in range(45):
        state=exchange();assert len(state['fighters'])==2 and all(p['kind']==1 for p in state['fighters']) and not state['failed'],state
        records.append(state);set_word('mp_banner_capture',1)
        for _ in range(300):
            time.sleep(.02)
            with socket.create_connection(('127.0.0.1',24689),3) as s:
                s.settimeout(5);packet(s,'?');receive(s)
                packet(s,f'm{symbols["mp_banner_capture"]:x},4');pending=int.from_bytes(bytes.fromhex(receive(s)),'little')
                packet(s,'c');packet(s,'D');receive(s)
            if not pending:break
        else:raise RuntimeError('Capture did not finish')
        name=f'banner-{i:04d}.bin';(out/name).write_bytes((sd/name).read_bytes())
        print(i,records[-1]['simulation'],(out/name).stat().st_size,flush=True)
        time.sleep(.02)
    (out/'states.json').write_text(json.dumps(records,indent=2)+'\n')
    bottom.OUT=out;bottom.capture('fox-ditto')

if __name__=='__main__':main()
