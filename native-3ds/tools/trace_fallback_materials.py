"""Capture fallback material inputs over complete rendered frames.

Read-only engine state; native submission breakpoints are removed on exit.
This measures work and captures compiler inputs, never physical timing.
"""
import argparse,hashlib,json,socket,struct
from collections import Counter
from pathlib import Path
from gameplay_test import TEST_ELF,symbols,packet,receive
from menu_display_pause_test import clock_state


def trace(args):
    initial=clock_state();assert initial['pause_flags'] or args.freeze_processes
    address=symbols['mp_native_submit'];rows=[];materials={};first=None
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(15)
        def rpc(text):packet(sock,text);return receive(sock)
        def read(a,n):
            raw=rpc(f'm{a:x},{n:x}');assert not raw.startswith('E'),raw
            data=bytes.fromhex(raw);assert len(data)==n;return data
        def words(name,n=1):return struct.unpack('>'+str(n)+'I',read(symbols[name],n*4))
        rpc('?');assert int.from_bytes(read(address,4),'little')&0xffff0000==0xe92d0000
        assert rpc(f'Z0,{address:x},4')=='OK'
        try:
            for _ in range(3000):
                assert rpc('c').startswith(('S','T'))
                regs=struct.unpack('<16I',bytes.fromhex(rpc('g'))[:64]);assert regs[15]==address
                frame=int.from_bytes(read(symbols['engine_frames'],4),'little')
                if first is None:first=frame
                if frame>first+2:break
                draw=struct.unpack('>34I',read(regs[2],136))
                row=dict(frame=frame,vertices=regs[1],gpu=bool(draw[20]),points=draw[28],layer=bool(draw[32]))
                if args.freeze_processes:
                    gobj=words('HSD_GObj_804D7814')[0]
                    if gobj:
                        head=read(gobj,8);row['object_kind']=list(head[:4])
                if not draw[20]:
                    state={name:list(words(name,count)) for name,count in
                        [('num_stages',1),('gpu_reject_reason',1),('flat_shading',1),
                         ('tev_configuration',480),('channel_configuration',36),('descriptors',26)]}
                    for name,count in [('tev_color',16),('konst_color',16),('material',8),('ambient',8)]:
                        state[name]=list(read(symbols[name],count))
                    state['shade_plan']=read(symbols['shade_plan'],136).hex()
                    key=hashlib.sha256(json.dumps(state,sort_keys=True).encode()).hexdigest()
                    materials[key]=state;row['material']=key
                rows.append(row)
                assert rpc(f'z0,{address:x},4')=='OK';assert rpc(f'Z0,{address+4:x},4')=='OK'
                assert rpc('c').startswith(('S','T'))
                assert struct.unpack('<16I',bytes.fromhex(rpc('g'))[:64])[15]==address+4
                assert rpc(f'z0,{address+4:x},4')=='OK';assert rpc(f'Z0,{address:x},4')=='OK'
            else:raise AssertionError('No two complete rendered frames')
        finally:
            rpc(f'z0,{address:x},4');rpc(f'z0,{address+4:x},4');packet(sock,'c');packet(sock,'D');receive(sock)
    final=clock_state()
    if not args.freeze_processes:assert final==initial,(initial,final)
    complete=[r for r in rows if r['frame']>first];assert len({r['frame'] for r in complete})==2
    usage=Counter()
    for row in complete:
        if not row['gpu']:usage[row['material']]+=row['vertices']
    result=dict(elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),complete_frames=2,
        submitted_vertices=sum(r['vertices'] for r in complete),fallback_vertices=sum(usage.values()),
        usage=usage.most_common(),materials=materials,rows=rows,clock=final,
        scope='Material trace with temporary object-process suspension; not a timing measurement' if args.freeze_processes else 'Read-only paused-scene material trace; not a timing measurement')
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('materials','rows')},indent=2),flush=True)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--freeze-processes',action='store_true',help='Temporarily suspend and restore object processes instead of original pause')
    args=ap.parse_args()
    if args.freeze_processes:
        from results_capture_scene_test import frozen_processes
        with frozen_processes():trace(args)
    else:trace(args)


if __name__=='__main__':main()
