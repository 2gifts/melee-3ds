"""Read one paused game render at the native submission boundary.

Only debugger breakpoints are written. Original game state and input remain
unchanged. Captures are private local validation data, not release assets.
"""
import argparse, hashlib, json, socket, struct, time
from pathlib import Path
from gameplay_test import ROOT, TEST_ELF, symbols, packet, receive
from menu_display_pause_test import clock_state


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    out=args.output;out.mkdir(parents=True,exist_ok=True)
    assert not (out/'manifest.json').exists(),'Use a new capture directory'
    initial=clock_state();assert initial['pause_flags'],'Requires an original paused match'
    bp=symbols['mp_native_submit'];rows=[];frame=None;capture_frame=None;started=time.monotonic()
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(15)
        def command(c):packet(s,c);return receive(s)
        def read(a,n):
            data=bytearray()
            while len(data)<n:
                take=min(4096,n-len(data));raw=command(f'm{a+len(data):x},{take:x}')
                assert not raw.startswith('E'),raw;data.extend(bytes.fromhex(raw))
            return bytes(data)
        def word(name):return int.from_bytes(read(symbols[name],4),'little')
        def store(name,data):
            (out/name).write_bytes(data);return dict(file=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        def advance(regs):
            nonlocal return_break
            assert command(f'z0,{bp:x},4')=='OK'
            # The current Azahar stub rejects single-step; return normally.
            return_break=(regs[14]&~1,2 if regs[14]&1 else 4)
            addr,size=return_break;assert command(f'Z0,{addr:x},{size}')=='OK'
            stop=command('c');assert stop.startswith('T05'),stop
            returned=bytes.fromhex(command('g'));assert struct.unpack_from('<I',returned,60)[0]==addr
            assert command(f'z0,{addr:x},{size}')=='OK';return_break=None
            assert command(f'Z0,{bp:x},4')=='OK'
        return_break=None
        command('?');assert command(f'Z0,{bp:x},4')=='OK'
        try:
            for index in range(4096):
                stop=command('c');assert stop.startswith('T05'),stop
                regs=bytes.fromhex(command('g'));r=struct.unpack('<16I',regs[:64])
                assert r[15]==bp,(hex(r[15]),hex(bp))
                current=word('engine_frames')
                if frame is None:frame=current
                if capture_frame is None:
                    if current==frame:advance(r);continue
                    capture_frame=current
                elif current!=capture_frame:break
                index=len(rows)
                vertex,count,state=r[:3];assert 0<count<=16384,count
                draw=read(state,136);d=struct.unpack('>34I',draw)
                record=dict(index=index,frame=current,count=count,draw=store(f'{index:04d}-draw.bin',draw))
                if d[20]:record['uniforms']=store(f'{index:04d}-uniforms.bin',read(d[20],1544))
                record['vertices']=store(f'{index:04d}-vertices.bin',read(vertex,count*56))
                if d[32]:record['layer']=store(f'{index:04d}-layer.bin',read(d[32],60))
                rows.append(record)
                if index%32==0:
                    (out/'progress.json').write_text(json.dumps(dict(batches=len(rows),seconds=time.monotonic()-started))+'\n')
                    print('Captured',len(rows),'batches',flush=True)
                advance(r)
            else:raise RuntimeError('Capture exceeded 4096 submissions in one render')
        finally:
            if return_break:
                addr,size=return_break;command(f'z0,{addr:x},{size}')
            command(f'z0,{bp:x},4');packet(s,'c');packet(s,'D');receive(s)
    final=clock_state();assert final['match_frame']==initial['match_frame'] and final['pause_flags']
    result=dict(batches=rows,initial_clock=initial,final_clock=final,complete_render_frame=capture_frame,
                elapsed_including_debugger=time.monotonic()-started,physical_fps_verified=False,
                elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest())
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Completed',len(rows),'batches; original match frame unchanged',flush=True)


if __name__=='__main__':main()
