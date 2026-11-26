"""Capture the stage select cursor's GX program from a running game.

Open the stage select (Versus, Stage Selection Turns, so the cursor shows its
port label) with Azahar's GDB stub enabled, then run this. It stops at display
lists while the cursor is drawn, keeps the first draw whose program is the
two-texture decal (two stages, stage 1 mixing CPREV toward TEXC by TEXA), and
writes build/cursor-live-material.json in tests/fixtures/cursor-material.json's
format. It then compares the two: the fixture is derived by
tools/derive_cursor_material.py from the decomp's TEV compiler, and this capture
confirms it. Observes only; never changes game or asset state."""
import json,socket,struct
from gameplay_test import ROOT,symbols,packet,receive

# Fields the comparison covers: map, channel, inputs, ops, swaps and konst
# selectors. Texture coordinate numbers depend on the texgen assignment.
COMPARED=range(1,25)


def main():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(15);packet(sock,'?');receive(sock)
        def rpc(q):packet(sock,q);return receive(sock)
        def read(a,n):
            data=b''
            while n:
                count=min(512,n);data+=bytes.fromhex(rpc(f'm{a:x},{count:x}'));a+=count;n-=count
            return data
        def word(a):return int.from_bytes(read(a,4),'big')
        bp=symbols['GXCallDisplayList'];assert rpc(f'Z0,{bp:x},4')=='OK'
        try:
            for i in range(20000):
                rpc('c')
                if word(symbols['num_stages'])==2:
                    tev=list(struct.unpack('>60I',read(symbols['tev_configuration'],60*4)))
                    if tev[30+3:30+7]==[0,8,9,15]:break
                # Azahar's step reply can precede actual interpreter progress.
                # Advance to the next instruction with a separate breakpoint.
                assert rpc(f'z0,{bp:x},4')=='OK';assert rpc(f'Z0,{bp+4:x},4')=='OK';rpc('c')
                assert rpc(f'z0,{bp+4:x},4')=='OK';assert rpc(f'Z0,{bp:x},4')=='OK'
                if i%500==0:print('Lists inspected',i,flush=True)
            else:raise RuntimeError('No two-texture decal draw found; is the stage select open?')
            result={'source':'captured: tools/trace_cursor_material.py','stages':2,
                    'tev_configuration':list(struct.unpack('>480I',read(symbols['tev_configuration'],480*4))),
                    'tev_color':read(symbols['tev_color'],16).hex(),'konst_color':read(symbols['konst_color'],16).hex(),
                    'texgens':word(symbols['texgens']),'stops':i}
            for name,count in [('texgen_configuration',8*5),('channel_configuration',36)]:
                result[name]=list(struct.unpack('>'+str(count)+'I',read(symbols[name],count*4)))
            result['textures']=read(symbols['textures'],8*32).hex()
        finally:
            rpc(f'z0,{bp:x},4');rpc(f'z0,{bp+4:x},4');packet(sock,'c');packet(sock,'D');receive(sock)
    (ROOT/'build/cursor-live-material.json').write_text(json.dumps(result,indent=2)+'\n')
    derived=json.loads((ROOT/'tests/fixtures/cursor-material.json').read_text())
    differences=[(s,f,derived['tev_configuration'][s*30+f],result['tev_configuration'][s*30+f])
                 for s in range(2) for f in COMPARED if derived['tev_configuration'][s*30+f]!=result['tev_configuration'][s*30+f]]
    for s,f,want,got in differences:print(f'stage {s} field {f}: derived {want}, captured {got}')
    print('Captured',result['tev_configuration'][:25],result['tev_configuration'][30:55],'texgens',result['texgens'])
    print('Konst',result['konst_color'],'registers',result['tev_color'])
    if differences:raise SystemExit('The captured cursor program differs from tests/fixtures/cursor-material.json')
    print('The captured cursor program matches the derived fixture')


if __name__=='__main__':main()
