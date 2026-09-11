"""Compare the real two-texture bridge against independently baked texels."""
import json,socket,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive
from efb_copy_test import SD


def state(trigger=False):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(8);packet(sock,'?');receive(sock)
        try:
            if trigger:
                packet(sock,f'M{symbols["layered_verify"]:x},4:01000000');assert receive(sock)=='OK'
            result={}
            for name in ['layered_verify','layered_draws','engine_failed','texture_barriers','texture_evictions']:
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)


def main():
    before=state(True);deadline=time.monotonic()+90
    while True:
        time.sleep(.5);after=state();assert not after['engine_failed'],after
        if not after['layered_verify']:break
        if time.monotonic()>deadline:raise TimeoutError('Layered GPU fixture stopped advancing')
    rows=[]
    for pressure in range(2):
        for sample in range(4):
            images=[np.fromfile(SD/f'layer-{pressure}-{sample}-{p}.bin',dtype=np.uint8).reshape(400,240,4) for p in range(2)]
            for p,pixels in enumerate(images):Image.fromarray(np.rot90(pixels[:,:,::-1])).save(ROOT/f'build/layer-{pressure}-{sample}-{p}.png')
            delta=np.abs(images[0].astype(int)-images[1].astype(int))
            row={'pressure':pressure,'sample':sample,'max_channel_error':int(delta.max()),'different_pixels':int(np.any(delta,axis=2).sum()),'distinct_colors':len(np.unique(images[0].reshape(-1,4),axis=0))}
            rows.append(row)
    result={'cases':rows,'before':before,'after':after,'independent_secondary_uv':True,'reference':'CPU-baked GX texels, nearest sampling; 8-bit combiner rounding tolerance 3'}
    (ROOT/'build/layered-gpu-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
    assert all(r['max_channel_error']<=3 and r['distinct_colors']>20 for r in rows),result
    assert after['layered_draws']>=before['layered_draws']+8
    assert after['texture_barriers']>before['texture_barriers'],result


if __name__=='__main__':main()
