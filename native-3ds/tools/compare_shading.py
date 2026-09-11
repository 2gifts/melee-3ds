"""Capture paused original gameplay with GPU and CPU vertex evaluation."""
import json,socket,subprocess,sys
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive
from combat_test import act

SD=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
def mode(cpu):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            packet(sock,f'M{symbols["gpu_disable"]:x},4:{int(cpu):08x}');assert receive(sock)=='OK'
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def capture(label,cpu):
    mode(cpu);act(0,3)
    subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True)
    data=(SD/'engine-top.bgr').read_bytes()
    im=Image.frombytes('RGB',(240,400),data,'raw','BGR').transpose(Image.Transpose.ROTATE_90)
    im.save(ROOT/f'build/shading-{label}.png');return np.asarray(im).astype(np.float32)

def difference(a,b):
    d=np.abs(a-b)
    return {'mean_absolute_byte_error':float(d.mean()),'rms_byte_error':float(np.sqrt((d*d).mean())),
            'fraction_pixels_over_8':float((d.max(axis=2)>8).mean())}

if __name__=='__main__':
    act(0x1000,2);act(0,30)
    try:
        a=capture('gpu-a',False);b=capture('gpu-b',False);c=capture('cpu',True);d=capture('gpu-c',False)
        report={'gpu_temporal':difference(a,b),'cpu_vs_gpu':difference(c,b),'gpu_return':difference(d,b)}
        (ROOT/'build/shading-comparison.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    finally:mode(False);act(0x1000,2)
