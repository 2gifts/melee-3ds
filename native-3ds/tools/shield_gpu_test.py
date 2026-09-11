"""Check actual PICA shield blends against independently baked reference images."""
import json,socket,time
import numpy as np
from gameplay_test import ROOT,symbols,packet,receive
from efb_copy_test import SD

def state(trigger=False):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(8);packet(sock,'?');receive(sock)
        try:
            if trigger:
                packet(sock,f'M{symbols["shield_verify"]:x},4:01000000');assert receive(sock)=='OK'
            result={}
            for name in ['shield_verify','engine_failed','texture_barriers','texture_evictions']:
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    before=state(True);deadline=time.monotonic()+90
    while True:
        time.sleep(.2);after=state();assert not after['engine_failed'],after
        if not after['shield_verify']:break
        if time.monotonic()>deadline:raise TimeoutError('Shield GPU fixture stopped advancing')
    rows=[]
    for mode in (1,2):
        for sample in range(36):
            images=[np.fromfile(SD/f'shield-{mode}-{sample}-{p}.bin',dtype=np.uint8).reshape(400,240,4) for p in range(2)]
            delta=np.abs(images[0].astype(int)-images[1].astype(int))
            rows.append({'mode':mode,'sample':sample,'max_channel_error':int(delta.max()),'different_pixels':int(np.any(delta,axis=2).sum())})
    result={'cases':rows,'before':before,'after':after,'reference':'CPU-baked original GX result; all six shield tints, two texture/pressure modes, three crossfade weights, two opacities','rounding_tolerance':3}
    (ROOT/'build/shield-gpu-test.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
    assert all(r['max_channel_error']<=3 for r in rows),rows
    assert after['texture_barriers']>before['texture_barriers'],result

if __name__=='__main__':main()
