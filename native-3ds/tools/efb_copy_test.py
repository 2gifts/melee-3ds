"""Compare GPU copy pixels with the exact source captured in that render pass."""
import argparse,json,socket,struct,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive

SD=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'

def flag(value=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            address=symbols['efb_verify']
            if value is not None:
                packet(sock,f'M{address:x},4:'+struct.pack('<I',value).hex())
                assert receive(sock)=='OK'
            packet(sock,f'm{address:x},4')
            return int.from_bytes(bytes.fromhex(receive(sock)),'little')
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def compare(fmt):
    q=struct.unpack('<14I',(SD/f'efb-{fmt:x}-0.bin').read_bytes())
    w,h=q[5:7];pw=max(8,1<<(w-1).bit_length());ph=max(8,1<<(h-1).bit_length())
    source=np.fromfile(SD/f'efb-{fmt:x}-1.bin',dtype=np.uint8).reshape(400,240,4)[:,:,::-1]
    actual=np.fromfile(SD/f'efb-{fmt:x}-2.bin',dtype=np.uint8).reshape(ph,pw,4)[:h,:w,::-1]
    width_file=SD/f'efb-{fmt:x}-width.txt'
    screen_width=int(width_file.read_text()) if width_file.exists() else 400
    sx=(400-screen_width)//2+(q[1]+np.arange(w)*q[3]//w)*screen_width//640
    sy=(q[2]+np.arange(h)*q[4]//h)*240//480
    expected=source[sx[None,:],239-sy[:,None]].copy()
    if fmt==0x20:expected[:]=((expected[:,:,:1]>>4)*17)
    else:
        for c,bits in enumerate((5,6,5)):
            value=expected[:,:,c]>>(8-bits)
            expected[:,:,c]=(value<<(8-bits))|(value>>(2*bits-8))
        expected[:,:,3]=255
    # Accelerated emulator surfaces can retain eight-bit channel values until
    # materialization. Compare in the destination format's actual precision.
    normalized=actual.copy()
    if fmt==0x20:normalized=(normalized>>4)*17
    else:
        for c,bits in enumerate((5,6,5)):
            value=normalized[:,:,c]>>(8-bits)
            normalized[:,:,c]=(value<<(8-bits))|(value>>(2*bits-8))
    diff=np.abs(normalized.astype(np.int16)-expected.astype(np.int16))
    metrics={'format':fmt,'extent':[w,h],'screen_width':screen_width,'source_rect':list(q[1:5]),
             'mean_error':float(diff.mean()),'max_error':int(diff.max()),
             'exact_pixels':float(np.all(diff==0,axis=2).mean()),
             'actual_range':[int(actual.min()),int(actual.max())],
             'expected_range':[int(expected.min()),int(expected.max())]}
    for name,data in (('source',source),('actual',actual),('expected',expected)):
        Image.fromarray(data).save(ROOT/f'build/efb-{fmt:x}-{name}.png')
    return metrics

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--format',choices=['shadow','refraction','both'],default='both');args=ap.parse_args()
    formats={'shadow':[0x20],'refraction':[4],'both':[0x20,4]}[args.format]
    try:
        flag(sum(2 if f==4 else 1 for f in formats));deadline=time.monotonic()+60
        while flag():
            if time.monotonic()>deadline:raise TimeoutError('Requested copy did not occur in this scene')
            time.sleep(.2)
    finally:flag(0)
    results=[compare(fmt) for fmt in formats]
    print(json.dumps(results,indent=2))
    (ROOT/'build/efb-copy-test.json').write_text(json.dumps(results,indent=2))
    assert all(r['max_error']==0 for r in results), 'GPU copy differs at destination color precision'

if __name__=='__main__':main()
