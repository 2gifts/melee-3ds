"""Read native submission inputs for complete frames; never mutate game state.

Requires an idle debugger connection and a paused battle. Breakpoint stops make
this unsuitable for timing: it measures the mix of vertex-lighting work only.
"""
import argparse,json,socket,struct,time
from pathlib import Path
from collections import Counter
from gameplay_test import symbols,packet,receive
from menu_display_pause_test import clock_state


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();initial_clock=clock_state();assert initial_clock['pause_flags']
    rows=[];address=symbols['mp_native_submit'];first=None
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(15)
        def cmd(text):packet(s,text);return receive(s)
        def read(address,size):
            value=cmd(f'm{address:x},{size:x}');assert not value.startswith('E'),value
            return bytes.fromhex(value)
        cmd('?')
        # This Azahar version explicitly rejects single-step (E5F). The
        # inspected ARM prologue is a nonbranching PUSH; break immediately
        # after it instead of depending on unsupported debugger stepping.
        assert int.from_bytes(read(address,4),'little')&0xffff0000==0xe92d0000
        assert cmd(f'Z0,{address:x},4')=='OK'
        try:
            for _ in range(2000):
                packet(s,'c');reply=receive(s);assert reply.startswith(('S','T')),reply
                regs=struct.unpack('<16I',bytes.fromhex(cmd('g'))[:64])
                assert regs[15]==address,hex(regs[15])
                frame=int.from_bytes(read(symbols['engine_frames'],4),'little')
                if first is None:first=frame
                if frame>first+2:break
                draw=struct.unpack('>34I',read(regs[2],136));gpu=draw[20]
                row=dict(frame=frame,vertices=regs[1],indices=draw[22],points=draw[28],layer=draw[32])
                if gpu:
                    data=read(gpu,1544);u=struct.unpack('>380f',data[:1520])
                    flat=int.from_bytes(data[1540:1544],'big')!=0
                    lighting=u[91*4]!=0 or u[92*4]!=0
                    row.update(route='flat' if flat else 'lit' if lighting else 'unlit',
                        channels=[u[91*4],u[92*4]],
                        lights=[dict(channel=u[(72+i)*4+3],position_w=u[(64+i)*4+3],
                            attenuation=u[(91+int(u[(72+i)*4+3]==2))*4+3],
                            diffuse_none=u[(91+int(u[(72+i)*4+3]==2))*4+1],
                            diffuse_clamp=u[(91+int(u[(72+i)*4+3]==2))*4+2])
                            for i in range(4) if u[(72+i)*4+3]>0])
                else:row['route']='cpu'
                rows.append(row)
                # Execute the entry instruction without immediately hitting
                # the same address again, then reinstate the breakpoint.
                assert cmd(f'z0,{address:x},4')=='OK'
                assert cmd(f'Z0,{address+4:x},4')=='OK'
                packet(s,'c');reply=receive(s);assert reply.startswith(('S','T')),reply
                assert struct.unpack('<16I',bytes.fromhex(cmd('g'))[:64])[15]==address+4
                assert cmd(f'z0,{address+4:x},4')=='OK'
                assert cmd(f'Z0,{address:x},4')=='OK'
            else:raise AssertionError('No complete render frames observed')
        finally:
            cmd(f'z0,{address:x},4');cmd(f'z0,{address+4:x},4')
            packet(s,'c');packet(s,'D');receive(s)
    complete=[r for r in rows if r['frame']>first]
    final_clock=clock_state()
    assert final_clock['pause_flags'] and final_clock['match_frame']==initial_clock['match_frame']
    assert len({r['frame'] for r in complete})==2 and complete
    draws=Counter();vertices=Counter();light_vertices=Counter()
    for row in complete:
        key=row['route']
        if key=='lit':key+=':'+str(len(row['lights']))+'lights'
        draws[key]+=1;vertices[key]+=row['vertices']
        if row['route']=='lit':
            for light in row['lights']:
                kind='distant' if light['position_w']==0 else 'local'
                kind+=':atten='+str(int(light['attenuation']))
                kind+=':diffuse='+('none' if light['diffuse_none'] else 'clamp' if light['diffuse_clamp'] else 'signed')
                light_vertices[kind]+=row['vertices']
    result=dict(passed=True,complete_frames=2,draws=dict(draws),vertices=dict(vertices),rows=rows,
                light_vertex_evaluations=dict(light_vertices),
                initial_clock=initial_clock,final_clock=final_clock,
                timing_valid=False,scope='Original native submissions before the two-eye duplication; paused encounter')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='rows'}),flush=True)


if __name__=='__main__':main()
