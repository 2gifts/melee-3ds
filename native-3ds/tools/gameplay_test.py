"""Exercise actual controller input in a running Training smoke-test build.

No fighter memory is changed. The result records positions, motion IDs and
damage from the original engine, alongside each bounded controller input.
"""
import json,socket,struct,subprocess,time
from gdb_probe import ROOT,packet,receive

listing=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),str(ROOT/'build/game/melee.elf')],text=True)
symbols={p[2]:int(p[0],16) for line in listing.splitlines() if len(p:=line.split())==3}

def exchange(control=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(address,size):
            packet(sock,f'm{address:x},{size:x}');return bytes.fromhex(receive(sock))
        def word(address,endian='big'):return int.from_bytes(read(address,4),endian)
        try:
            if control:
                buttons,frames,x,y=control
                data=struct.pack('<IIIii',time.time_ns()&0xffffffff,buttons,frames,x,y)
                packet(sock,f'M{symbols["mp_test_control"]:x},{len(data):x}:'+data.hex())
                assert receive(sock)=='OK'
            result={'frame':word(symbols['engine_frames'],'little'),'failed':word(symbols['engine_failed'],'little'),'fighters':[]}
            result['simulation']=word(symbols['gm_80479D58'])
            result['stage_kind']=word(symbols['selected_stage'])
            head=word(symbols['HSD_GObjPLinkHead']);gobj=word(head+32)
            while gobj and len(result['fighters'])<8:
                fp=word(gobj+0x2c);kind=word(fp+4)
                if kind>32:raise RuntimeError('The original match is not active')
                result['fighters'].append({'kind':kind,'motion':word(fp+0x10),'airborne':bool(word(fp+0xe0)),'position':struct.unpack('>fff',read(fp+0xb0,12)),'damage':struct.unpack('>f',read(fp+0x1830,4))[0]})
                gobj=word(gobj+8)
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    records=[]
    actions=[('run right',0,50,80,0),('release',0,10,0,0),('jump',0x400,3,0,0),('rise',0,8,0,0),('neutral attack',0x100,6,0,0),('land',0,35,0,0),('jab',0x100,3,0,0),('release',0,30,0,0)]
    try:
        initial=exchange();records.append({'initial':initial});print(json.dumps(records[-1]),flush=True)
        if initial['failed'] or len(initial['fighters'])<2:raise RuntimeError('A live two-fighter Training match is required')
        for label,*control in actions:
            start=exchange(control);deadline=time.monotonic()+150
            while True:
                time.sleep(.5);state=exchange()
                if state['failed']:raise RuntimeError(f'Engine stopped during {label}: {state}')
                if state['frame']>=start['frame']+control[1]:break
                if time.monotonic()>deadline:raise TimeoutError(f'No frame progress during {label}')
            records.append({'action':label,'input':control,'result':state});print(json.dumps(records[-1]),flush=True)
    finally:
        (ROOT/'build/gameplay-test.json').write_text(json.dumps(records,indent=2))

if __name__=='__main__':main()
