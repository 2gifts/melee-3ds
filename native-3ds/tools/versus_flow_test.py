"""Observe a regular timed Versus match through its original results scene."""
import json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive

def observe(control=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        blocks={}
        def read(a,n):
            result=b''
            while n:
                base=a&~511;offset=a-base;size=min(n,512-offset)
                if base not in blocks:
                    packet(sock,f'm{base:x},200');blocks[base]=bytes.fromhex(receive(sock))
                    if len(blocks[base])!=512:raise RuntimeError(f'Invalid match block {base:x}')
                result+=blocks[base][offset:offset+size];a+=size;n-=size
            return result
        def word(a,endian='big'):return int.from_bytes(read(a,4),endian)
        try:
            if control:
                data=struct.pack('<IIIii',time.time_ns()&0xffffffff,*control)
                packet(sock,f'M{symbols["mp_test_control"]:x},{len(data):x}:'+data.hex());assert receive(sock)=='OK'
            route=read(symbols['state_machine'],6);controller=read(symbols['controller'],0x30)
            state={'mode':route[0],'scene':route[3],'frame':word(symbols['engine_frames'],'little'),
                   'failed':word(symbols['engine_failed'],'little'),'status':controller[0],
                   'match_over':controller[14],'timer':int.from_bytes(controller[40:44],'big'),
                   'simulation':word(symbols['gm_80479D58']),'fighters':[],
                   'texture_bytes':word(symbols['texture_bytes'],'little')}
            if route[0]==2 and route[3]==4:
                result=read(symbols['lbl_8046DBE8'],0x98)
                end=word(symbols['lbl_8046DBE8']+0x94)
                state['results_phase']=result[1]
                state['standings']=[list(read(end+0x58+i*0xa8,8)) for i in range(4)] if end else []
            if route[0]==2 and route[3] in (2,3):
                head=word(symbols['HSD_GObjPLinkHead']);gobj=word(head+32)
                for _ in range(8):
                    if not gobj:break
                    fp=word(gobj+44)
                    state['fighters'].append({'kind':word(fp+4),'motion':word(fp+16),
                        'damage':struct.unpack('>f',read(fp+0x1830,4))[0]})
                    gobj=word(gobj+8)
            return state
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    records=[];deadline=time.monotonic()+600;last_frame=None;progress=time.monotonic()
    try:
        while time.monotonic()<deadline:
            state=observe();records.append(state);print(json.dumps(state),flush=True)
            if state['failed']:raise RuntimeError('Original Versus engine stopped')
            if state['frame']!=last_frame:last_frame=state['frame'];progress=time.monotonic()
            elif time.monotonic()-progress>20:raise RuntimeError('Original Versus engine stopped advancing')
            if state['mode']!=2:raise RuntimeError('Regular Versus mode required')
            if state['scene']==4:
                print('Original Versus match reached its results scene',flush=True);return
            time.sleep(2)
        raise TimeoutError('Timed Versus match did not finish')
    finally:(ROOT/'build/versus-flow-test.json').write_text(json.dumps(records,indent=2))

if __name__=='__main__':main()
