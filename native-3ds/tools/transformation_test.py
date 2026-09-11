"""Verify Zelda/Sheik transformations using down-special controller input."""
import json,socket,time
from gameplay_test import ROOT,symbols,packet,receive,exchange
from combat_test import act,records
from cpu_attack_test import set_cpu_behavior

def player():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        def word(a):return int.from_bytes(read(a,4),'big')
        try:
            base=symbols['player_slots'];form=read(base+12,2)
            gobj=word(base+0xb0+4*form[0]);fp=word(gobj+44)
            return {'form':list(form),'kind':word(fp+4),'motion':word(fp+16)}
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    set_cpu_behavior(0)
    # A fighter can be hanging from the stage after the preceding combat run.
    act(0x400,2,0,0);act(0,18,60,0);act(0,25)
    first=player();records.append({'initial_form':first});assert first['kind'] in (19,7),first
    for expected in (7 if first['kind']==19 else 19,first['kind']):
        for _ in range(5):
            act(0x200,2,0,-80);act(0,25)
            current=player();records.append({'observed_form':current})
            if current['kind']==expected:break
        else:raise RuntimeError('Down-special did not switch the active player entity')
        act(0,20)
    assert player()['form']==first['form']
    print('Original Zelda/Sheik down-special switched both ways and restored the active player form')
if __name__=='__main__':
    try:main()
    finally:(ROOT/'build/transformation-test.json').write_text(json.dumps(records,indent=2))
