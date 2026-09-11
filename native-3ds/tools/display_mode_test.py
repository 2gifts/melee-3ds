"""Exercise the live 4:3/expanded switch and verify the actual framebuffers."""
import argparse,json,socket,struct,subprocess,sys,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive
from profile_switch import set_word
from efb_copy_test import SD

def inspect():
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s)
        def read(a,n):packet(s,f'm{a:x},{n:x}');return bytes.fromhex(receive(s))
        try:
            result={'native_mode':int.from_bytes(read(symbols['mp_native_expanded'],4),'little'),
                    'engine_mode':int.from_bytes(read(symbols['mp_display_expanded'],4),'big'),
                    'failed':int.from_bytes(read(symbols['engine_failed'],4),'little')}
            camera=int.from_bytes(read(symbols['world_camera'],4),'big');assert camera
            result['camera']=camera;result['fov_aspect']=struct.unpack('>2f',read(camera+0x40,8))
            result['view_matrix']=struct.unpack('>12f',read(camera+0x54,48))
            return result
        finally:packet(s,'c');packet(s,'D');receive(s)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--minimum-side-pixels',type=int,default=1000);args=ap.parse_args()
    results=[]
    try:
        for mode,label in [(0,'4-3'),(1,'expanded'),(0,'4-3-return')]:
            set_word('mp_native_expanded',mode);time.sleep(.3)
            subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True,capture_output=True)
            data=np.fromfile(SD/'engine-top.bgr',dtype=np.uint8).reshape(400,240,3)[:,:,::-1]
            pixels=np.rot90(data);Image.fromarray(pixels).save(ROOT/f'build/performance-{label}.png')
            outside=np.concatenate((pixels[:,:40],pixels[:,360:]),axis=1)
            result=inspect();result.update(label=label,outside_nonblack=int(np.any(outside!=0,axis=2).sum()))
            assert not result['failed'] and result['engine_mode']==mode,result
            if not mode:assert not result['outside_nonblack'],result
            else:assert result['outside_nonblack']>args.minimum_side_pixels,result
            results.append(result)
    finally:set_word('mp_native_expanded',0)
    assert results[0]['fov_aspect']==results[1]['fov_aspect']==results[2]['fov_aspect'],'Camera tracking parameters changed'
    (ROOT/'build/display-mode-test.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
if __name__=='__main__':main()
