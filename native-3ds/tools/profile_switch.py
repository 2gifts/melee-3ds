"""A/B an isolated development-build option in the same live match."""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive
from profile_render_detail import snapshot,summarize

def set_word(name,value,endian='little'):
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s)
        try:
            packet(s,f'M{symbols[name]:x},4:'+value.to_bytes(4,endian).hex());assert receive(s)=='OK'
        finally:packet(s,'c');packet(s,'D');receive(s)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('symbol');ap.add_argument('--big',action='store_true');ap.add_argument('--seconds',type=int,default=6);args=ap.parse_args()
    results=[]
    try:
        for value in (1,0,1,0):
            set_word(args.symbol,value,'big' if args.big else 'little');time.sleep(.5)
            start=snapshot(True);time.sleep(args.seconds);end=snapshot(False)
            result={'value':value,**summarize(start,end)};results.append(result);print(json.dumps(result),flush=True)
    finally:set_word(args.symbol,0,'big' if args.big else 'little')
    (ROOT/'build'/f'profile-{args.symbol}.json').write_text(json.dumps(results,indent=2))
if __name__=='__main__':main()
