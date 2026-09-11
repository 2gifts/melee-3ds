"""Read actual save defaults and exercise UCF through the native pad bridge."""
import json,socket,struct
from gameplay_test import ROOT,symbols,packet,receive
from combat_test import act

def snapshot():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        try:
            base=int.from_bytes(read(symbols['gmMainLib_804D3EE0'],4),'big')
            rules=read(base+0x1850,24);save=read(base+0x1868,8);prefs=read(base+0x1cb0,28)
            return {'rules':list(rules),'character_mask':int.from_bytes(save[:2],'big'),
                'stage_mask':int.from_bytes(save[2:4],'big'),'features':save[4],
                'item_frequency':prefs[0],'items':int.from_bytes(prefs[8:16],'big'),
                'random_stages':int.from_bytes(prefs[24:28],'big'),
                'ucf_enabled':int.from_bytes(read(symbols['mp_ucf_enabled'],4),'big'),
                'checks':struct.unpack('>8I',read(symbols['mp_ucf_checks'],32)),
                'applied':struct.unpack('>8I',read(symbols['mp_ucf_applied'],32))}
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    start=snapshot()
    assert start['rules'][2]==1 and start['rules'][4:12]==[4,0,10,0,8,1,0,0],start
    assert start['character_mask']==2047 and start['stage_mask']==2047 and start['features']==255,start
    assert start['item_frequency']==255 and start['items']==0 and start['random_stages']==0xe70000b0,start
    assert start['ucf_enabled']==1,start
    for _ in range(8):
        act(0,12);act(0,1,80);act(0,1,-80);act(0,6);act(0,1,0,-80);act(0,4)
    end=snapshot();assert end['checks'][0]>start['checks'][0]+100,end
    assert end['checks'][1]>start['checks'][1],end
    result={'initial':start,'final':end,'defaults_verified':True,'native_input_hook_active':True}
    (ROOT/'build/qol-live-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
