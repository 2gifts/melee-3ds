"""Exercise visible original menus through controller input on a fresh boot.

Retain screenshots for review. A portrait-region brightness check specifically
rejects the blank CSS photographed on hardware; state transitions alone cannot
establish that a menu is visible.
"""
import json, subprocess, sys, time
import numpy as np
from PIL import Image
from select_test_stage import ROOT, observe, act, open_mode, select_character

SD=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
records=[]

def capture(label, css=False):
    subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True)
    image=Image.frombytes('RGB',(240,400),(SD/'engine-top.bgr').read_bytes(),'raw','BGR').transpose(Image.Transpose.ROTATE_90)
    path=ROOT/f'build/menu-fix-{label}.png';image.save(path)
    pixels=np.asarray(image);region=pixels[35:112,60:341]
    bright=int((region.max(axis=2)>150).sum())
    records.append({'screen':label,'path':str(path),'state':observe(),'bright_portrait_pixels':bright})
    print(json.dumps({'screen':label,'bright_portrait_pixels':bright}),flush=True)
    if css:assert bright>6000, 'Character portraits are missing, as in the hardware regression'

def main():
    deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        try:s=observe()
        except (ConnectionRefusedError,ConnectionResetError):time.sleep(.1);continue
        assert not s['failed'],s
        if s['frame']>=450 and 'menu' in s:
            s=act();break
        time.sleep(.1)
    else:raise TimeoutError('Fresh boot did not reach main menu')
    s=act(frames=35);capture('main')
    while s['menu']['kind']==0:
        s=act(0x100 if s['menu']['selection']==1 else 4 if s['menu']['selection']<1 else 8)
        s=act(frames=30)
    assert s['menu']['kind']==2,s
    capture('versus-menu')
    s=open_mode(s,True);s=act(frames=45);capture('characters',css=True)
    s=select_character(s,0,1);s=select_character(s,1,1)
    capture('ready',css=True)
    s=act(0x1000,4);s=act(frames=45)
    assert 'cursor' in s and not s['failed'],s
    capture('stages')

if __name__=='__main__':
    try:main()
    finally:(ROOT/'build/menu-visual-test.json').write_text(json.dumps(records,indent=2)+'\n')
