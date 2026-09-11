"""Exercise the companion screen with real menu/game input in Azahar.

Touch injection is confined to the development build's native UI. Game
objects, player state, stocks, damage and original code are never modified.
"""
import json,socket,struct,subprocess,sys,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT,symbols,packet,receive
from profile_switch import set_word
import select_test_stage as select

OUT=ROOT/'build/update11-qa'
FIELDS='scene mode stock_mode teams seconds timer stage rule_stocks rule_minutes items stamina menu'.split()
PLAYER='kind character costume color stocks damage'.split()

def snapshot():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
        def word(name):return int.from_bytes(read(symbols[name],4),'little')
        try:
            words=struct.unpack('<36I',read(symbols['mp_bottom_observed'],144));s=dict(zip(FIELDS,words))
            s['players']=[dict(zip(PLAYER,words[12+i*6:18+i*6])) for i in range(4)]
            for n in ('engine_frames','engine_failed','mp_bottom_redraws','mp_bottom_draw_ticks','mp_bottom_fps_visible','mp_bottom_guide_visible','mp_native_expanded'):
                s[n]=word(n)
            return s
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def capture(label):
    subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True,capture_output=True)
    source=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
    a=np.rot90(np.fromfile(source/'engine-bottom.rgb565',dtype='<u2').reshape(320,240)).astype(np.uint32)
    rgb=np.array([(a>>11)*255//31,((a>>5)&63)*255//63,(a&31)*255//31],dtype=np.uint8).transpose(1,2,0)
    OUT.mkdir(exist_ok=True);Image.fromarray(rgb).save(OUT/f'{label}-bottom.png')
    top=np.rot90(np.fromfile(source/'engine-top.bgr',dtype=np.uint8).reshape(400,240,3)[:,:,::-1])
    Image.fromarray(top).save(OUT/f'{label}-top.png')
    result=snapshot();assert not result['engine_failed'];(OUT/f'{label}.json').write_text(json.dumps(result,indent=2))
    print(label,result,flush=True);return result

def touch(x,y,field,expected):
    set_word('mp_test_bottom_touch',x|(y<<16));deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        s=snapshot()
        if s[field]==expected:return s
        time.sleep(.1)
    raise AssertionError((field,expected,s))

def choose(state,slot,icon):
    # Short automated pad pulses can land between original input polls.
    # Retry only through observed CSS input; never force a selection field.
    for attempt in range(3):
        try:return select.select_character(state,slot,icon)
        except RuntimeError:
            state=select.observe()
            if 'hand' not in state or state['failed'] or attempt==2:raise
            print('Retrying CSS input for slot',slot+1,flush=True)
            state=select.act(frames=15)

def main():
    OUT.mkdir(exist_ok=True);set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',0)
    results=[]
    select.observe((0,1,0,0))
    saved_title=False
    for _ in range(90):
        state=select.observe()
        if 'menu' in state or 'hand' in state:break
        s=snapshot()
        if s['scene']==0 and not saved_title:results.append(capture('title'));saved_title=True
        select.act(0x100 if s['scene']==42 else 0x1000,2);select.act(frames=30)
    else:raise AssertionError('Main menu did not open')
    if 'menu' in state:results.append(capture('menu'))
    state=select.open_versus(state)
    for slot,icon in enumerate((1,6,16,23)):
        state=choose(state,slot,icon)
    s=capture('four-player-css');assert s['scene']==8 and [p['kind'] for p in s['players']]==[0,1,1,1];results.append(s)
    bounds=state['doors'][3]['toggle'];state=select.move_hand(state,((bounds[0]+bounds[1])/2,-2.2))
    select.act(0x100,1);state=select.act(frames=20)
    s=capture('closed-css');assert s['players'][3]['kind']==3;results.append(s)
    state=choose(state,3,23)
    select.act(0x1000,4);select.act(frames=30);results.append(capture('stage-select'))
    old=sys.argv;sys.argv=['select_test_stage.py','25']
    try:select.main()
    finally:sys.argv=old
    select.act(frames=150)
    s=capture('four-player-match');assert s['scene']==2 and s['stock_mode'] and all(p['kind']!=3 for p in s['players']);results.append(s)
    touch(45,225,'mp_bottom_fps_visible',1);results.append(capture('fps-on'))
    touch(270,225,'mp_bottom_guide_visible',1);results.append(capture('controls'))
    touch(270,225,'mp_bottom_guide_visible',0)
    touch(160,225,'mp_native_expanded',1);results.append(capture('expanded'))
    touch(160,225,'mp_native_expanded',0);touch(45,225,'mp_bottom_fps_visible',0)
    # Walk off Final Destination until the original stock counter drops.
    stocks=s['players'][0]['stocks'];deadline=time.monotonic()+60
    select.observe((0,60000,80,0))
    while time.monotonic()<deadline:
        s=snapshot()
        if s['players'][0]['stocks']<stocks:break
        time.sleep(.2)
    else:raise AssertionError('No stock loss observed')
    select.act(frames=2);results.append(capture('stock-loss'))
    start=snapshot();time.sleep(4);end=snapshot()
    count=end['mp_bottom_redraws']-start['mp_bottom_redraws'];ticks=end['mp_bottom_draw_ticks']-start['mp_bottom_draw_ticks']
    results.append({'updates':count,'average_update_ms':ticks/40500/count if count else 0,'sample_frames':end['engine_frames']-start['engine_frames']})
    (OUT/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Four-player live display, scene transitions, stocks and touch controls passed.',flush=True)

if __name__=='__main__':main()
