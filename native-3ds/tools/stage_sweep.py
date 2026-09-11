"""Walk stages and fighters through the real Training/CSS/SSS input path."""
import argparse,json,socket,struct,sys,time,subprocess
from gameplay_test import ROOT,symbols,packet,receive,exchange
import select_test_stage as select
from cpu_attack_test import menu
from profile_render_detail import snapshot,summarize

def exit_training():
    for _ in range(12):
        if menu()['open']:break
        select.act(0x1000,2);select.act(frames=12)
    else:raise RuntimeError('Training menu did not open')
    for _ in range(12):
        if menu()['selection']==8:break
        select.act(8,1);select.act(frames=2)
    else:raise RuntimeError('Training Exit was not selected')
    select.act(0x100,2)
    deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        state=select.observe()
        if state['failed']:raise RuntimeError(state)
        if 'hand' in state:return state
        time.sleep(.1)
    raise TimeoutError('Training did not return to character selection')

def metrics():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            result={}
            for name,endian in [('render_vertex_count','little'),('draw_count','little'),('mp_geometry_cull_draws','big'),('mp_geometry_cull_checks','big'),('texture_bytes','little'),('native_geometry_bytes','little'),('geometry_bytes','big'),('command_barriers','little'),('texture_evictions','little')]:
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),endian)
            for name in ('mp_collision_matrix_repairs','mp_collision_parent_repairs','mp_collision_matrix_failed','mp_collision_invalid_count'):
                if name in symbols:
                    packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'big')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def capture(stage,directory='stage-sweep'):
    import numpy as np
    from PIL import Image
    subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True,capture_output=True)
    data=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee/engine-top.bgr'
    pixels=np.rot90(np.fromfile(data,dtype=np.uint8).reshape(400,240,3)[:,:,::-1])
    path=ROOT/f'build/{directory}/stage-{stage:02}.png';path.parent.mkdir(parents=True,exist_ok=True)
    Image.fromarray(pixels).save(path);return str(path)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--stages',default='25,24,28,8,0,1,2,3,4,5,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,26,27');ap.add_argument('--seconds',type=int,default=4);ap.add_argument('--label',default='all-stage-sweep');ap.add_argument('--character-offset',type=int,default=2);ap.add_argument('--image-directory',default='stage-sweep');args=ap.parse_args()
    records=[]
    try:
        for order,stage in enumerate(map(int,args.stages.split(','))):
            state=select.observe()
            if 'hand' not in state:exit_training()
            character=(order+args.character_offset)%25;cpu=16 if character!=16 else 10
            old_argv=sys.argv;sys.argv=['select_test_stage.py',str(stage),'--character',str(character),'--cpu',str(cpu)]
            try:select.main()
            finally:sys.argv=old_argv
            # Use game input for movement, a jump and a special before the
            # uninterrupted measurement; never write fighter/stage state.
            select.act(frames=125)
            for buttons,frames,x,y in [(0x400,2,0,0),(0,12,0,0),(0x200,3,0,0),(0,45,0,0)]:select.act(buttons,frames,x,y)
            initial=exchange();start=snapshot(True);time.sleep(args.seconds);end=snapshot(False)
            result={'stage_index':stage,'character_icon':character,'cpu_icon':cpu,'initial':initial,'final':exchange(),
                'profile':summarize(start,end),'metrics':metrics(),'image':capture(stage,args.image_directory),'input_only':True}
            assert not result['final']['failed'] and result['profile']['same_scene'] and len(result['final']['fighters'])>=2,result
            records.append(result);print(json.dumps(result),flush=True)
            (ROOT/f'build/{args.label}.json').write_text(json.dumps(records,indent=2))
            assert result['metrics'].get('mp_collision_matrix_failed',0)==0,result
            assert result['metrics'].get('mp_collision_invalid_count',0)==0,result
    finally:
        (ROOT/f'build/{args.label}.json').write_text(json.dumps(records,indent=2))
if __name__=='__main__':main()
