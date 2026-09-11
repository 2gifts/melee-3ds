"""Check both geometry caches and LRU selection during real input-driven play."""
import argparse,json,socket,subprocess,sys
from gameplay_test import ROOT,symbols
from gdb_probe import packet,receive
from profile_switch import set_word
from combat_test import act

FLAGS={'native_geometry_lru_validate':'little','native_geometry_validate':'little',
       'geometry_compare_validate':'big'}
COUNTERS={'native_geometry_lru_checks':'little','native_geometry_checks':'little',
          'geometry_compare_checks':'big','geometry_checks':'big','shade_checks':'big'}

def counters():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            result={}
            for name,endian in COUNTERS.items():
                packet(sock,f'm{symbols[name]:x},4')
                result[name]=int.from_bytes(bytes.fromhex(receive(sock)),endian)
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=3)
    ap.add_argument('--native-only',action='store_true',help='Retain normal decoded-cache behavior to stress native eviction/reuse')
    args=ap.parse_args();initial=counters()
    try:
        for name,endian in FLAGS.items():set_word(name,1,endian)
        if args.native_only:
            for i in range(args.seconds):
                act((0x400,0x100,0x200)[i%3],3);act(0,57)
        else:
            subprocess.run([sys.executable,str(ROOT/'tools/geometry_cache_test.py'),'--seconds',str(args.seconds)],check=True)
        final=counters();delta={name:final[name]-initial[name] for name in initial}
        required=('native_geometry_lru_checks','native_geometry_checks','geometry_compare_checks') if args.native_only else COUNTERS
        assert min(delta[name] for name in required)>100,delta
        result={'initial':initial,'final':final,'checks':delta,'controller_input_only':True,'native_only':args.native_only}
        (ROOT/'build/native-geometry-test.json').write_text(json.dumps(result,indent=2)+'\n')
        print('Native vertex/index reuse, source comparison, LRU and decoded cache checks passed:',json.dumps(delta),flush=True)
    finally:
        for name,endian in FLAGS.items():set_word(name,0,endian)

if __name__=='__main__':main()
