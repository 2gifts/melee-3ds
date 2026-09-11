"""Pull and throw turnips through Peach's actual down-B input."""
import argparse,json,socket,struct,subprocess,sys
from gameplay_test import ROOT,symbols,packet,receive,exchange
import select_test_stage as select
from stage_sweep import exit_training,capture


def held_item():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        def word(a):return int.from_bytes(read(a,4),'big')
        try:
            gobj=word(word(symbols['HSD_GObjPLinkHead'])+32)
            while gobj:
                fp=word(gobj+0x2c)
                if word(fp+4)==9:
                    item=word(fp+0x1974)
                    if not item:return {'held':False}
                    data=word(item+0x2c)
                    return {'held':True,'gobj':item,'data':data,'kind':word(data+0x10),'face':word(data+0xdd8)}
                gobj=word(gobj+8)
            raise RuntimeError('Peach was not found')
        finally:packet(sock,'c');packet(sock,'D');receive(sock)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--prepare',action='store_true');ap.add_argument('--fresh',action='store_true');ap.add_argument('--count',type=int,default=1);ap.add_argument('--label',default='turnip-before');args=ap.parse_args()
    if args.prepare:
        if not args.fresh:exit_training()
        subprocess.run([sys.executable,str(ROOT/'tools/select_test_stage.py'),'25','--character','4','--cpu','16',*(['--fresh'] if args.fresh else [])],check=True)
    select.act(frames=120);results=[]
    for i in range(args.count):
        if held_item()['held']:
            select.act(0x100,3);select.act(frames=90)
        select.act(frames=4,y=-80);select.act(0x200,4,y=-80);select.act(frames=100)
        item=held_item();assert item['held'],item
        state=exchange();assert not state['failed'],state
        row={'item':item,'state':state,'image':capture(i,args.label),'input_only':True}
        results.append(row);print(json.dumps(row),flush=True)
        (ROOT/f'build/{args.label}.json').write_text(json.dumps(results,indent=2))


if __name__=='__main__':main()
