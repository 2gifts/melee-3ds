"""Compare cached and freshly decoded geometry during controller-driven play."""
import argparse,json,socket,struct
from gameplay_test import ROOT,symbols,exchange
from gdb_probe import packet,receive
from combat_test import act,records

def cache_state(validate=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            if validate is not None:
                for name in ('geometry_validate','shade_validate'):
                    data=struct.pack('>I',int(validate));packet(sock,f'M{symbols[name]:x},4:'+data.hex());assert receive(sock)=='OK'
            result={}
            for name in ('geometry_hits','geometry_misses','geometry_checks','geometry_bytes','shade_hits','shade_misses','shade_checks'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int(receive(sock),16)
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=15);args=ap.parse_args()
    initial=cache_state(True)
    try:
        records.append({'initial':exchange(),'cache':initial})
        # Jump, attack, special, shield and move through the original pad path.
        # Limit horizontal input to keep this comparison on the selected stage.
        for i in range(args.seconds):
            button=(0x400,0x100,0x200,0x20,0)[i%5]
            act(button,3,(-1 if i&1 else 1)*20);act(0,57)
        final=cache_state();assert final['geometry_checks']>initial['geometry_checks']+100
        assert final['shade_checks']>initial['shade_checks']+100
        records.append({'cache':final});print('Animated geometry matches fresh decoding:',json.dumps(final),flush=True)
    finally:
        cache_state(False)
        (ROOT/'build/geometry-cache-test.json').write_text(json.dumps(records,indent=2))
