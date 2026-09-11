"""Create a name through the original keyboard, inspect glyphs, then open CSS."""
import json,socket,time
import numpy as np
from PIL import Image
from match_fix_menu_test import select,capture,detail
from select_test_stage import ROOT,observe,act,open_mode,select_character,symbols,packet,receive
import menu_visual_test as visual

def current_name():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        packet(sock,'?');receive(sock)
        try:
            packet(sock,f'm{symbols["mnNameNew_CurrentNameText"]:x},10');return bytes.fromhex(receive(sock))
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    deadline=time.monotonic()+150
    while time.monotonic()<deadline:
        try:s=observe()
        except ConnectionRefusedError:time.sleep(.1);continue
        assert not s['failed'],s
        if s['frame']>=450 and 'menu' in s:act();break
        time.sleep(.1)
    else:raise TimeoutError('Main menu')
    select(0,1);select(2,4)
    act(0x100);act(frames=40);capture('keyboard-sjis')
    # Previously every letter cell was empty. Count gold letter interiors,
    # excluding the gold borders and the selected orange cell.
    im=np.asarray(Image.open(ROOT/'build/menu-fix-fix3-keyboard-sjis.png'))
    mask=(im[:,:,0]>65)&(im[:,:,0]>im[:,:,2]*1.6)&(im[:,:,1]>35)
    glyphs=sum(int(mask[y+5:y+16,x+5:x+18].sum()) for x in range(66,265,24) for y in range(69,160,23))
    assert glyphs>150,('Keyboard glyphs are missing',glyphs)
    act(0x100);act(frames=15);name=current_name()
    assert name[:3]==b'\x82\x60\0',name.hex() # initial English key: full-width A
    capture('typed-name');act(0x1000);act(frames=15);act(0x100);act(frames=50)
    capture('saved-name');assert detail()['kind']==18,detail()
    act(0x200);act(frames=40)
    s=open_mode(observe(),True);s=act(frames=40)
    s=select_character(s,0,10);s=select_character(s,1,19)
    capture('fighter-names')
    print(json.dumps({'keyboard_letter_pixels':glyphs,'typed_name_shift_jis':name.hex(),'mode':'Fox/Pikachu Versus CSS'}),flush=True)

if __name__=='__main__':
    try:main()
    finally:(ROOT/'build/name-entry-test.json').write_text(json.dumps(visual.records,indent=2)+'\n')
