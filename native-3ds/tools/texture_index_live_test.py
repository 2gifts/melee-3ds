"""Compare indexed lookup with the original scan, including forced eviction."""
import argparse,json,socket,subprocess,sys,time
from gameplay_test import ROOT,symbols,packet,receive
from profile_render_detail import snapshot,summarize

def state(**changes):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(8);packet(sock,'?');receive(sock)
        try:
            for name,value in changes.items():
                packet(sock,f'M{symbols[name]:x},4:'+value.to_bytes(4,'little').hex());assert receive(sock)=='OK'
            result={}
            for name in ('texture_lookup_disable','texture_lookup_validate','texture_lookup_checks','texture_count','texture_bytes','texture_barriers','engine_frames','engine_failed'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=10);ap.add_argument('--pressure',action='store_true');args=ap.parse_args()
    initial=state();result={'initial':initial,'windows':[]}
    try:
        state(texture_lookup_validate=1,texture_lookup_disable=0)
        if args.pressure:subprocess.run([sys.executable,str(ROOT/'tools/texture_budget_test.py')],check=True)
        else:time.sleep(args.seconds)
        checked=state(texture_lookup_validate=0)
        assert not checked['engine_failed'] and checked['texture_lookup_checks']>initial['texture_lookup_checks']+100,checked
        result['validation']=checked;print('Lookup validation:',json.dumps(checked),flush=True)
        # Give texture residency and timing a chance to recover from pressure.
        time.sleep(3)
        for disabled in (1,0,1):
            state(texture_lookup_disable=disabled);start=snapshot(True)
            try:time.sleep(args.seconds)
            finally:end=snapshot(False)
            measured={'lookup':'linear' if disabled else 'indexed',**summarize(start,end)}
            result['windows'].append(measured);print(json.dumps(measured),flush=True)
    finally:
        result['final']=state(texture_lookup_disable=initial['texture_lookup_disable'],texture_lookup_validate=initial['texture_lookup_validate'])
        (ROOT/'build/texture-index-live-test.json').write_text(json.dumps(result,indent=2))
if __name__=='__main__':main()
