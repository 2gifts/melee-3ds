"""Exercise CSS slot close/reopen and token deselection through real input."""
import argparse,json,time,subprocess,sys,socket
from pathlib import Path
import select_test_stage as select
from gameplay_test import ROOT
from stage_sweep import capture

def portrait_visibility():
    from gameplay_test import symbols,packet,receive
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock);cache={}
        def read(a,n):
            out=b''
            while n:
                base=a&~511;off=a-base;k=min(n,512-off)
                if base not in cache:
                    packet(sock,f'm{base:x},200');cache[base]=bytes.fromhex(receive(sock))
                    assert len(cache[base])==512
                out+=cache[base][off:off+k];a+=k;n-=k
            return out
        def word(a):return int.from_bytes(read(a,4),'big')
        try:
            root=word(symbols['mnCharSel_804D6CC0']);nodes=[];seen=set()
            def walk(joint):
                while joint:
                    assert joint not in seen and len(seen)<400;seen.add(joint)
                    flags=word(joint+0x14);nodes.append(flags)
                    if not flags&0x1000:walk(word(joint+0x10))
                    joint=word(joint+8)
            walk(root);result=[]
            for i in range(2):
                door=read(symbols['mnCharSel_803F0DFC']+36*i,36)
                result.append({'kind':door[11],'emblem_hidden':bool(nodes[door[0]]&16),
                               'costume_hidden':bool(nodes[door[1]]&16)})
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--label',default='update9-css-before');ap.add_argument('--stereo',type=int,default=0);args=ap.parse_args()
    from profile_switch import set_word
    set_word('mp_test_stereo_slider',args.stereo)
    rows=[]
    state=select.observe()
    if 'hand' not in state:
        for _ in range(12):
            if 'menu' in state:break
            select.act(0x1000,2);state=select.act(frames=30)
        state=select.open_mode(state,True)
    def save(name):
        state=select.act(frames=40)
        row={'name':name,'state':state,'image':capture(len(rows),args.label),
             'visibility':portrait_visibility()}
        if args.stereo:
            import numpy as np
            from PIL import Image
            source=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee/engine-right.bgr'
            pixels=np.rot90(np.fromfile(source,dtype=np.uint8).reshape(400,240,3)[:,:,::-1])
            path=ROOT/f'build/{args.label}/stage-{len(rows):02}-right.png'
            Image.fromarray(pixels).save(path);row['right_image']=str(path)
            left=np.asarray(Image.open(row['image']))
            row['different_eye_pixels']=int(np.any(left!=pixels,axis=2).sum())
            assert row['different_eye_pixels']>100,row
        if name=='slot-closed':
            assert row['visibility'][1]['costume_hidden'] and row['visibility'][1]['emblem_hidden'],row
        elif name in ('ready','slot-reopened'):
            assert not row['visibility'][1]['costume_hidden'] and not row['visibility'][1]['emblem_hidden'],row
        rows.append(row);print(json.dumps(row),flush=True)
        (ROOT/f'build/{args.label}.json').write_text(json.dumps(rows,indent=2))
        return state
    state=select.select_character(state,0,23);state=select.select_character(state,1,6)
    state=save('ready')
    bounds=state['doors'][1]['toggle'];state=select.move_hand(state,((bounds[0]+bounds[1])/2,-2.2))
    select.act(0x100,1);state=select.act(frames=10)
    state=save('slot-closed');assert state['doors'][1]['kind']==3,state['doors']
    state=select.select_character(state,1,6);state=save('slot-reopened')
    token=state['doors'][1]['xy'];state=select.move_hand(state,(token[0]-3.8,token[1]+2.6))
    select.act(0x100,1);state=select.act(frames=10)
    save('token-lifted')

if __name__=='__main__':main()
