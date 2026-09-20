"""Read-only live fighter damage and stage-item hitbox snapshots."""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive

def observe(*, include_hitboxes=True):
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s);cache={}
        def read(a,n):
            out=b''
            while n:
                base=a&~511;off=a-base;k=min(n,512-off)
                if base not in cache:
                    packet(s,f'm{base:x},200');cache[base]=bytes.fromhex(receive(s))
                    assert len(cache[base])==512,(base,len(cache[base]))
                out+=cache[base][off:off+k];a+=k;n-=k
            return out
        def word(a,endian='big'):return int.from_bytes(read(a,4),endian)
        def floats(a,n):return struct.unpack('>'+n*'f',read(a,n*4))
        try:
            out={'frame':word(symbols['engine_frames'],'little'),'simulation':word(symbols['gm_80479D58']),'failed':word(symbols['engine_failed'],'little'),
                 'stage_id':word(symbols['selected_stage']),'ground_kind':word(symbols['stage_info']+0x88),'fighters':[],'items':[]}
            head=word(symbols['HSD_GObjPLinkHead'])
            for link in (8,9):
                g=word(head+link*4);seen=set()
                while g:
                    assert g not in seen and len(seen)<100;seen.add(g);p=word(g+0x2c)
                    if link==8:
                        out['fighters'].append({'gobj':g,'kind':word(p+4),'position':floats(p+0xb0,3),'damage':floats(p+0x1830,1)[0],'source':word(p+0x1868),'collision':floats(p+0x1854,3),'since_hit':word(p+0x18ac)})
                    else:
                        row={'gobj':g,'kind':word(p+0x10),'motion':word(p+0x24),'position':floats(p+0x4c,3),'hitboxes':[]}
                        for i in range(4 if include_hitboxes else 0):
                            h=p+0x5d4+0x13c*i
                            row['hitboxes'].append({'state':word(h),'damage':floats(h+12,1)[0],'radius':floats(h+28,1)[0],'position':floats(h+0x4c,3),'previous':floats(h+0x58,3)})
                        out['items'].append(row)
                    g=word(g+8)
            return out
        finally:packet(s,'c');packet(s,'D');receive(s)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=float,default=30);ap.add_argument('--label',default='damage-source');args=ap.parse_args()
    rows=[];deadline=time.monotonic()+args.seconds
    try:
        while time.monotonic()<deadline:
            r=observe();assert not r['failed'];rows.append(r)
            if len(rows)==1 or [f['damage'] for f in rows[-2]['fighters']]!=[f['damage'] for f in r['fighters']]:print(json.dumps(r),flush=True)
            time.sleep(.15)
    finally:(ROOT/f'build/{args.label}.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':main()
