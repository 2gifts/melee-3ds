"""Compare PICA geometry-shader points against rasterized reference quads."""
import argparse,hashlib,json,socket,struct,time
from pathlib import Path
import numpy as np
from PIL import Image
from gameplay_test import ROOT,TEST_ELF,symbols,packet,receive
from profile_switch import set_word
from texture_visibility_test import read
SD=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
def flag(value=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            address=symbols['point_verify']
            if value is not None:
                packet(sock,f'M{address:x},4:'+struct.pack('<I',value).hex());assert receive(sock)=='OK'
            packet(sock,f'm{address:x},4');return int.from_bytes(bytes.fromhex(receive(sock)),'little')
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,default=ROOT/'build')
    ap.add_argument('--stereo',action='store_true',help='Require the stereo program, including its point GS entry')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    if args.stereo:
        set_word('mp_test_stereo_slider',1000)
        deadline=time.monotonic()+30
        while not read(('stereo_active',))['stereo_active']:
            if time.monotonic()>deadline:raise TimeoutError('Stereo renderer did not become active')
            time.sleep(.25)
    before=read(('stereo_active','engine_failed'));assert not before['engine_failed'],before
    flag(1);deadline=time.monotonic()+60
    while flag():
        if time.monotonic()>deadline:raise TimeoutError('Point fixture did not finish')
        time.sleep(.25)
    images=[np.fromfile(SD/f'points-{i}.bin',dtype=np.uint8).reshape(256,256,4)[:,:,::-1] for i in range(2)]
    for name,data in zip(('expected','actual'),images):Image.fromarray(data).save(args.output/f'points-{name}.png')
    diff=np.abs(images[0].astype(np.int16)-images[1].astype(np.int16))
    background=np.array([0x12,0x34,0x56,0xff],dtype=np.uint8)
    foreground=[int(np.any(im!=background,axis=2).sum()) for im in images]
    after=read(('stereo_active','engine_failed'))
    result={'cases':64,'max_error':int(diff.max()),'different_pixels':int(np.any(diff,axis=2).sum()),'mean_error':float(diff.mean()),
            'foreground_pixels':foreground,'before':before,'after':after,'elf_sha256':hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
            'physical_console_tested':False}
    (args.output/'point-gpu-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    assert not after['engine_failed'] and before['stereo_active']==after['stereo_active'],result
    assert not args.stereo or after['stereo_active'],result
    assert min(foreground)>100,'Empty point fixture cannot establish equivalence'
    assert result['max_error']==0,result
if __name__=='__main__':main()
