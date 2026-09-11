"""Check actual PICA blending against independent GX arithmetic."""
import json,socket,struct,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive
SD=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'

def flag(value=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            address=symbols['blend_verify']
            if value is not None:
                packet(sock,f'M{address:x},4:'+struct.pack('<I',value).hex());assert receive(sock)=='OK'
            packet(sock,f'm{address:x},4');return int.from_bytes(bytes.fromhex(receive(sock)),'little')
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    flag(1);deadline=time.monotonic()+60
    while flag():
        if time.monotonic()>deadline:raise TimeoutError('Blend fixture did not finish')
        time.sleep(.25)
    images=[np.fromfile(SD/f'blending-{i}.bin',dtype=np.uint8).reshape(256,256,4)[:,:,::-1] for i in range(2)]
    for name,data in zip(('actual','expected'),images):Image.fromarray(data).save(ROOT/f'build/blending-{name}.png')
    diff=np.abs(images[0].astype(np.int16)-images[1].astype(np.int16))
    result={'cases':148,'max_error':int(diff.max()),'pixels_over_one_unit':int(np.any(diff>1,axis=2).sum()),'mean_error':float(diff.mean())}
    (ROOT/'build/blend-gpu-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    assert result['max_error']<=1,result
if __name__=='__main__':main()
