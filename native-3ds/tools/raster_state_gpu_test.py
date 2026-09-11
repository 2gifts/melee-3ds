"""Require pixel equality with redundant-state submission enabled/disabled."""
import json,socket,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive
from efb_copy_test import SD

def state(**changes):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(8);packet(sock,'?');receive(sock)
        try:
            for name,value in changes.items():
                packet(sock,f'M{symbols[name]:x},4:'+value.to_bytes(4,'little').hex());assert receive(sock)=='OK'
            result={}
            for name in ('raster_state_verify','raster_state_disable','raster_state_hits','raster_state_updates','engine_failed','engine_frames'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    before=state(raster_state_verify=1);deadline=time.monotonic()+60
    while True:
        time.sleep(.5);after=state();assert not after['engine_failed'],after
        if not after['raster_state_verify']:break
        if time.monotonic()>deadline:raise TimeoutError('Raster state comparison stopped advancing')
    images=[np.fromfile(SD/f'raster-state-{i}.bin',dtype=np.uint8).reshape(400,240,4)[:,:,::-1] for i in range(2)]
    for i,data in enumerate(images):Image.fromarray(np.rot90(data)).save(ROOT/f'build/raster-state-{i}.png')
    different=int(np.any(images[0]!=images[1],axis=2).sum());colors=len(np.unique(images[0].reshape(-1,4),axis=0))
    result={'cases':160,'draws_per_case':3,'different_pixels':different,'distinct_colors':colors,'before':before,'after':after}
    (ROOT/'build/raster-state-gpu-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
    assert not different and colors>50,result
if __name__=='__main__':main()
