"""Enable CPU Attack through observed Training menus, then verify live combat."""
import argparse,json,socket,struct,time
from combat_test import act,records
from gameplay_test import ROOT,exchange,symbols,packet,receive

def menu():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        try:
            base=symbols['gm_80473814'];header=read(base,4);values=struct.unpack('>7i',read(base+0x1e0,28))
            return {'selection':header[0],'open':header[1],'cpu_type':header[3],'values':values}
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def set_cpu_behavior(option=4):
    # START is ignored during the original fighter entrance animation.
    deadline=time.monotonic()+120
    while exchange()['simulation']<120:
        if time.monotonic()>deadline:raise TimeoutError('Training startup did not finish')
        time.sleep(.5)
    for _ in range(10):
        if menu()['open']:break
        act(0x1000,2);act(0,12)
    else:raise RuntimeError('Training pause menu did not open')
    for _ in range(10):
        state=menu()
        if state['selection']==3:break
        act(4,2);act(0,3)
    else:raise RuntimeError('CPU menu entry did not become selected')
    for _ in range(7):
        state=menu()
        if state['values'][3]==option:break
        act(2,2);act(0,3)
    else:raise RuntimeError('Requested CPU behavior did not become selected')
    act(0x1000,2);act(0,6);state=menu()
    assert not state['open'] and state['cpu_type']==option,state
    records.append({'confirmed_training_menu':state});print(json.dumps(records[-1]),flush=True)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=15);args=ap.parse_args()
    try:
        records.append({'initial':exchange()});set_cpu_behavior(4)
        initial=exchange();start=initial['simulation'];deadline=time.monotonic()+300;observed_damage=False
        for i in range(args.seconds):
            while True:
                state=exchange()
                if state['failed']:raise RuntimeError(state)
                if state['simulation']>=start+(i+1)*60:break
                if time.monotonic()>deadline:raise TimeoutError('CPU simulation stopped advancing')
                time.sleep(.5)
            observed_damage|=any(f['damage']>0 for f in state['fighters'])
            records.append({'simulation_seconds':i+1,'state':state});print(json.dumps(records[-1]),flush=True)
        assert observed_damage,'CPU Attack was enabled but the observation did not demonstrate damage'
        print(f'Original Training CPU completed {args.seconds*60} simulation updates with observed damage',flush=True)
    except BaseException:
        records.append({'failure':exchange()});raise
    finally:(ROOT/'build/cpu-attack-test.json').write_text(json.dumps(records,indent=2))
if __name__=='__main__':main()
