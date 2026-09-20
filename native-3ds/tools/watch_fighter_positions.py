"""Stop at the first nonfinite fighter position/velocity using GDB watchpoints."""
import argparse,bisect,json,math,re,socket,struct,subprocess,time
from pathlib import Path
from gameplay_test import ROOT,symbols,packet,receive
import bottom_screen_test as bottom
import select_test_stage as select
from profile_switch import set_word
from damage_source_test import observe


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=120)
    ap.add_argument('--no-rotation-cache',action='store_true');args=ap.parse_args()
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    if args.no_rotation_cache:set_word('mp_rotation_cache',0,'big')
    deadline=time.monotonic()+args.seconds
    while bottom.snapshot()['scene']!=2:
        assert time.monotonic()<deadline
        time.sleep(.1)
    actors=observe()['fighters']
    table=[]
    nm=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),'-n',str(ROOT/'build/game/melee.elf')],text=True)
    for line in nm.splitlines():
        p=line.split()
        if len(p)==3 and p[1] in ('t','T') and not p[2].startswith(('$','mp_be_fix_')):table.append((int(p[0],16),p[2]))
    addresses=[a for a,n in table]
    def label(a):
        i=bisect.bisect_right(addresses,a)-1
        return table[i][1]+'+'+hex(a-table[i][0]) if i>=0 else hex(a)
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(10);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        watchers={};writes=0;trace=[]
        for actor in actors:
            ptr=int.from_bytes(read(actor['gobj']+0x2c,4),'big')
            for offset in (0xb0,):
                a=ptr+offset;watchers[a]={'fighter':ptr,'gobj':actor['gobj'],'kind':actor['kind'],'field':hex(offset),'previous':read(a,4).hex()}
                packet(sock,f'Z2,{a:x},4');assert receive(sock)=='OK'
        print('Watching',json.dumps(watchers),flush=True)
        def refresh():
            head=int.from_bytes(read(symbols['HSD_GObjPLinkHead'],4),'big')
            current={};g=int.from_bytes(read(head+8*4,4),'big') if head else 0
            for _ in range(12):
                if not g:break
                data=read(g,48);p=int.from_bytes(data[44:48],'big')
                if p:
                    kind=int.from_bytes(read(p+4,4),'big');a=p+0xb0
                    current[a]={'fighter':p,'gobj':g,'kind':kind,'field':'0xb0','previous':read(a,4).hex()}
                g=int.from_bytes(data[8:12],'big')
            for a in set(watchers)-set(current):packet(sock,f'z2,{a:x},4');receive(sock);del watchers[a]
            for a in set(current)-set(watchers):
                packet(sock,f'Z2,{a:x},4');assert receive(sock)=='OK';watchers[a]=current[a]
            return current
        try:
            while time.monotonic()<deadline:
                packet(sock,'c');stop=receive(sock)
                assert stop.startswith('T'),stop
                writes+=1
                if writes==1:print('Stop packet:',stop,flush=True)
                match=re.search(r'(?:r?watch|awatch):([0-9a-f]+)',stop)
                selected=int(match[1],16) if match else None
                for a,row in watchers.items():
                    if selected is not None and selected!=a:continue
                    raw=read(a,4);changed=raw.hex()!=row['previous']
                    if changed:
                        trace.append({'address':hex(a),'before':row['previous'],'after':raw.hex(),
                            'stop':stop})
                        trace=trace[-20:];row['previous']=raw.hex()
                    if not math.isfinite(struct.unpack('>f',raw)[0]):
                        # Scene teardown fills freed allocations with FF.
                        # Never mistake that allocator poison for a fighter.
                        current=refresh()
                        if a not in current:break
                        registers=struct.unpack('<16I',read_registers(sock)[:64])
                        stack=read(registers[13],512)
                        result={'watch_writes':writes,'watched':watchers,'bad_address':hex(a),'registers':list(registers),
                            'pc':label(registers[15]),'lr':label(registers[14]),'recent':trace,
                            'instructions':read(registers[15]-32,64).hex(),'stack':stack.hex(),
                            'fighter':read(row['fighter'],0x300).hex(),
                            'stack_symbols':[label(v) for v in struct.unpack('>128I',stack) if 0x100000<=v<0x500000]}
                        output=ROOT/'build/hardware/2026-09-11-update14/diagnosis/first-invalid-position.json'
                        output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True);return
                if writes%128==0:refresh()
            print('Recent writes:',json.dumps(trace),'Total',writes,flush=True)
            raise TimeoutError('No invalid write observed before deadline')
        finally:
            for a in watchers:packet(sock,f'z2,{a:x},4');receive(sock)
            packet(sock,'c');packet(sock,'D');receive(sock)


def read_registers(sock):packet(sock,'g');return bytes.fromhex(receive(sock))
if __name__=='__main__':main()
