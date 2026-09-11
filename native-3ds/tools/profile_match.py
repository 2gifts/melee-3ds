"""Read original simulation/render counters against the emulated VI clock."""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive

def observe():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(name,n):
            packet(sock,f'm{symbols[name]:x},{n:x}');return bytes.fromhex(receive(sock))
        try:
            sim,render,match=struct.unpack('>3I',read('gm_80479D58',12))
            return {'simulation':sim,'render':render,'match':match,
                    'ticks':int.from_bytes(read('last_retrace',4),'big'),
                    'failed':int.from_bytes(read('engine_failed',4),'little')}
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=5);args=ap.parse_args()
    start=observe();time.sleep(args.seconds);end=observe()
    seconds=((end['ticks']-start['ticks'])&0xffffffff)/40500000
    result={'start':start,'end':end,'emulated_seconds':seconds,
            'rates':{k:(end[k]-start[k])/seconds for k in ('simulation','render','match')}}
    assert not end['failed'] and all(end[k]>start[k] for k in ('simulation','render','match')),result
    (ROOT/'build/match-profile.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

if __name__=='__main__':main()
