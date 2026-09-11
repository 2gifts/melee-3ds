"""Compare new/original CPU copy bytes and timings on live captured frames."""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive
from profile_switch import set_word

def snapshot():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        def word(a,endian='little'):return int.from_bytes(read(a,4),endian)
        try:
            result={name:word(symbols[name]) for name in ('engine_frames','engine_failed','efb_rgb565_disable','efb_rgb565_validate','efb_rgb565_checks','efb_rgb565_pixels','texture_uploads','texture_evictions')}
            result['ticks']=struct.unpack('<2I',read(symbols['efb_rgb565_ticks'],8))
            # StageInfo.map_gobjs starts at 0x180; Stadium display is map 1.
            gobj=word(symbols['stage_info']+0x184,'big')
            if gobj:
                ground=word(gobj+0x2c,'big')
                result['screen_state']=list(struct.unpack('>3h',read(ground+0xe4,6)))
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--copies',type=int,default=128);args=ap.parse_args()
    initial=snapshot();samples=[];deadline=time.monotonic()+180
    try:
        set_word('efb_rgb565_disable',0);set_word('efb_rgb565_validate',1)
        while True:
            time.sleep(.5);end=snapshot();samples.append(end)
            assert not end['engine_failed'],end
            if end['efb_rgb565_checks']-initial['efb_rgb565_checks']>=args.copies:break
            if time.monotonic()>deadline:raise TimeoutError('Insufficient natural RGB565 captures')
        copies=end['efb_rgb565_checks']-initial['efb_rgb565_checks']
        ticks=[(a-b)&0xffffffff for a,b in zip(end['ticks'],initial['ticks'])]
        result={'copies':copies,'pixels':end['efb_rgb565_pixels']-initial['efb_rgb565_pixels'],
                'fast_ms':ticks[0]/40500,'reference_ms':ticks[1]/40500,
                'scope':'Paired conversions of identical live ARM framebuffer data; pixel equality includes all padded bytes',
                'samples':samples}
        assert ticks[0]>0 and ticks[1]>0,result
        (ROOT/'build/efb-rgb565-live-test.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2),flush=True)
    finally:
        set_word('efb_rgb565_validate',initial['efb_rgb565_validate'])
        set_word('efb_rgb565_disable',initial['efb_rgb565_disable'])

if __name__=='__main__':main()
