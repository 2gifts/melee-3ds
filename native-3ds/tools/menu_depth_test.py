"""Verify both eye images of the actual main/Versus menus, leaving VS CSS open."""
import json,sys,time
import numpy as np
from PIL import Image
import select_test_stage as select
from gameplay_test import ROOT
from profile_switch import set_word
from stage_sweep import capture

def main():
    deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        try:s=select.observe()
        except (ConnectionRefusedError,ConnectionResetError):time.sleep(.1);continue
        assert not s['failed'],s
        if s['frame']>=5:break
        time.sleep(.1)
    else:raise TimeoutError('Fresh boot did not begin rendering')
    set_word('mp_test_frame_limit',0)
    set_word('mp_test_stereo_slider',1000)
    s=select.act(frames=35)
    for _ in range(20):
        if 'menu' in s and s['menu']['state']==0:break
        select.act(0x1000,2);s=select.act(frames=30)
    else:raise RuntimeError('Main menu did not appear')
    assert s['menu']['kind']==0,s
    rows=[]
    def save(name):
        select.act(frames=40);left=capture(len(rows),'update9-menus')
        source=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee/engine-right.bgr'
        right=np.rot90(np.fromfile(source,dtype=np.uint8).reshape(400,240,3)[:,:,::-1])
        path=ROOT/f'build/update9-menus/stage-{len(rows):02}-right.png';Image.fromarray(right).save(path)
        pixels=int(np.any(np.asarray(Image.open(left))!=right,axis=2).sum())
        row={'screen':name,'state':select.observe(),'left':left,'right':str(path),'different_eye_pixels':pixels}
        rows.append(row);(ROOT/'build/update9-menu-depth.json').write_text(json.dumps(rows,indent=2)+'\n')
        assert pixels>100,row
        print(json.dumps(row),flush=True)
    save('main')
    for _ in range(8):
        if s['menu']['kind']==2:break
        s=select.act(0x100 if s['menu']['selection']==1 else 4 if s['menu']['selection']<1 else 8)
        s=select.act(frames=30)
    assert s['menu']['kind']==2,s
    save('versus');select.open_mode(s,True)
    print('Perspective main and Versus menus produce distinct eye views',flush=True)

if __name__=='__main__':main()
