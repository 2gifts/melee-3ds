"""Observe a shield's original color and GX material through controller input."""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive
import select_test_stage as select


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--label',default='shield-material-before')
    ap.add_argument('--start-effect',action='store_true');args=ap.parse_args()
    select.act(frames=30)
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(15);packet(sock,'?');receive(sock)
        def rpc(q):packet(sock,q);return receive(sock)
        def read(a,n):
            data=b''
            while n:
                count=min(512,n);data+=bytes.fromhex(rpc(f'm{a:x},{count:x}'));a+=count;n-=count
            return data
        def word(a):return int.from_bytes(read(a,4),'big')
        def registers():return struct.unpack('<16I',bytes.fromhex(rpc('g'))[:64])
        spawn=symbols['efLib_SetTevKonstColor'];draw=symbols['GXCallDisplayList'];active=[]
        def advance(bp):
            # Continuing at an Azahar entry breakpoint otherwise hits it again.
            assert rpc(f'z0,{bp:x},4')=='OK';active.remove(bp)
            assert rpc(f'Z0,{bp+4:x},4')=='OK';active.append(bp+4)
            rpc('c')
            assert rpc(f'z0,{bp+4:x},4')=='OK';active.remove(bp+4)
            assert rpc(f'Z0,{bp:x},4')=='OK';active.append(bp)
        try:
            assert rpc(f'Z0,{spawn:x},4')=='OK';active.append(spawn)
            data=struct.pack('<IIIii',time.time_ns()&0xffffffff,0x40,60000,0,0)
            assert rpc(f'M{symbols["mp_test_control"]:x},{len(data):x}:'+data.hex())=='OK'
            for _ in range(12):
                rpc('c');regs=registers()
                if args.start_effect or regs[1]==0:break
                advance(spawn)
            else:raise RuntimeError('Steady shield did not spawn')
            result={'shield_joint':regs[0],'texture_index':regs[1],
                    'konst_rgb':regs[2],'player_rgb':regs[3],'objects':[]}
            targets=[];dobj=word(regs[0]+24)
            while dobj:
                mat=word(dobj+8);pobj=word(dobj+12)
                while pobj:
                    display=word(pobj+16);targets.append(display)
                    result['objects'].append({'dobj':dobj,'mobj':mat,'pobj':pobj,'display':display})
                    pobj=word(pobj+4)
                dobj=word(dobj+4)
            assert targets,result
            (ROOT/f'build/{args.label}-objects.json').write_text(json.dumps(result,indent=2)+'\n')
            assert rpc(f'z0,{spawn:x},4')=='OK';active.remove(spawn)
            assert rpc(f'Z0,{draw:x},4')=='OK';active.append(draw)
            for i in range(6000):
                rpc('c');regs=registers()
                if regs[0] in targets:break
                advance(draw)
            else:raise RuntimeError('Shield display list was not drawn')
            result['display_stop_count']=i;result['display']=regs[0];result['display_bytes']=regs[1]
            result['stages']=word(symbols['num_stages'])
            for name,count in [('tev_configuration',16*30),('texgen_configuration',8*5),('channel_configuration',36),('swap_table',16)]:
                if name not in symbols:continue
                values=struct.unpack('>'+str(count)+'I',read(symbols[name],count*4))
                result[name]=list(values)
            for name in ['texgens','ind_count','current_mtx','projection_type']:
                if name not in symbols:continue
                result[name]=word(symbols[name])
            for name,size in [('tev_color',16),('konst_color',16),('material',8),('color_s10',32),('textures',8*32)]:
                if name not in symbols:continue
                result[name]=read(symbols[name],size).hex()
            result['frame']=int.from_bytes(read(symbols['engine_frames'],4),'little')
            result['stereo_active']=int.from_bytes(read(symbols['stereo_active'],4),'little')
            assert rpc(f'M{symbols["mp_test_capture"]:x},4:01000000')=='OK'
            (ROOT/f'build/{args.label}.json').write_text(json.dumps(result,indent=2)+'\n')
            print(json.dumps({k:v for k,v in result.items() if k not in ('tev_configuration','texgen_configuration','channel_configuration','textures')},indent=2),flush=True)
        finally:
            for bp in active:rpc(f'z0,{bp:x},4')
            packet(sock,'c');packet(sock,'D');receive(sock)
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        time.sleep(.03)
        with socket.create_connection(('127.0.0.1',24689),3) as sock:
            packet(sock,'?');receive(sock);packet(sock,f'm{symbols["mp_test_capture"]:x},4');pending=receive(sock)
            packet(sock,'c');packet(sock,'D');receive(sock)
        if pending=='00000000':break
    else:raise TimeoutError('Shield capture did not finish')
    import numpy as np
    from PIL import Image
    sd=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee'
    for eye,name in [('left','engine-top.bgr'),('right','engine-right.bgr')]:
        if eye=='right' and not result['stereo_active']:continue
        if not (sd/name).exists():continue
        pixels=np.rot90(np.fromfile(sd/name,dtype=np.uint8).reshape(400,240,3)[:,:,::-1])
        Image.fromarray(pixels).save(ROOT/f'build/{args.label}-{eye}.png')
    select.act(frames=2)


if __name__=='__main__':main()
