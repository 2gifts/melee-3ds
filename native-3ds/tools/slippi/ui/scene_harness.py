"""Boot the development build, start a Classic round and capture its intro splash
(both eyes) several times. Usage: classic_splash.py ROUND LABEL [STEREO] [--play SECONDS]"""
import os,sys,time,socket,subprocess,shutil,types,zlib,struct
from pathlib import Path
ROOT=Path(r'C:\Users\kirby\Melee Decomp\melee-3ds\native-3ds')
os.environ.setdefault('MP_TEST_ELF',str(ROOT/'build/game-opt/melee.elf'))
sys.path.insert(0,str(ROOT/'tools'))
# The harness imports numpy/PIL only for its own screenshots; stub them.
for name in ('numpy','PIL','PIL.Image'):sys.modules[name]=types.ModuleType(name)
sys.modules['PIL'].Image=sys.modules['PIL.Image']

def save_bgr(path,out):
    """Rotate the 400x240 column-major BGR dump to a 400x240 RGB PNG."""
    data=path.read_bytes();w,h=400,240;rows=[]
    for y in range(h):
        row=bytearray(b'\x00')
        for x in range(w):
            i=(x*240+(239-y))*3;row+=bytes((data[i+2],data[i+1],data[i]))
        rows.append(bytes(row))
    raw=b''.join(rows)
    def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    out.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b''))

EMU=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1'
SD=EMU/'user/sdmc/3ds/melee'
round_number=int(sys.argv[1]);label=sys.argv[2];stereo=int(sys.argv[3]) if len(sys.argv)>3 else 1000
play=float(sys.argv[sys.argv.index('--play')+1]) if '--play' in sys.argv else 0
OUT=ROOT/'build/update28-qa';OUT.mkdir(parents=True,exist_ok=True)
subprocess.run(['taskkill','/F','/IM','azahar.exe'],capture_output=True);time.sleep(2)
(SD/'game.log').unlink(missing_ok=True)
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
proc=subprocess.Popen([str(EMU/'azahar.exe'),str(ROOT/'dist/3ds/melee/melee-development.3dsx')],startupinfo=si)
for _ in range(300):
    try:
        with socket.create_connection(('127.0.0.1',24689),.2):break
    except OSError:time.sleep(.1)
import select_test_stage as select
import bottom_screen_test as bottom
from gameplay_test import symbols,packet,receive
from profile_switch import set_word
from menu_refinement_test import enter

def sample_profile(seconds,label):
    import re,random
    raw=[]
    with socket.create_connection(('127.0.0.1',24689),5) as s:
        s.settimeout(20);packet(s,'?');receive(s)
        packet(s,'qXfer:threads:read::0,fff');threads=re.findall(r'id="([0-9a-fA-F]+)"',receive(s))
        packet(s,'c');end=time.monotonic()+seconds
        while time.monotonic()<end:
            time.sleep(random.uniform(.02,.05))
            s.sendall(b'');receive(s)
            for t in threads:
                packet(s,'Hg'+t)
                if receive(s)!='OK':continue
                packet(s,'pf');pc=int.from_bytes(bytes.fromhex(receive(s)),'little')
                packet(s,'pe');lr=int.from_bytes(bytes.fromhex(receive(s)),'little')
                raw.append((t,pc,lr))
            packet(s,'c')
        s.sendall(b'');receive(s);packet(s,'c');packet(s,'D');receive(s)
    (ROOT/'build/perf'/f'{label}-samples.txt').write_text(chr(10).join(f'{t} {pc:08x} {lr:08x}' for t,pc,lr in raw))
    print('samples',len(raw),flush=True)

def grab(name):
    subprocess.run([sys.executable,str(ROOT/'tools/capture_game.py')],check=True,capture_output=True)
    for eye,fname in (('L','engine-top.bgr'),('R','engine-right.bgr')):
        path=SD/fname
        if path.exists():save_bgr(path,OUT/f'{label}-{name}-{eye}.png')
    print('captured',name,flush=True)

try:
    set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',stereo)
    if '--wide' in sys.argv:set_word('mp_native_expanded',1)
    for item in filter(None,os.environ.get('SET_WORDS','').split(',')):
        name,value=item.split('=');set_word(name,int(value,0),'big' if value.endswith('b') else 'little') if False else set_word(name,int(value.rstrip('b'),0),'big' if value.endswith('b') else 'little')
    select.observe((0,1,0,0))
    for _ in range(70):
        state=select.observe()
        if 'menu' in state:break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2)
        select.act(frames=20)
    else:raise AssertionError(state)
    if '--stage' in sys.argv:
        stage=int(sys.argv[sys.argv.index('--stage')+1])
        old_argv=sys.argv;sys.argv=['select_test_stage.py',str(stage),'--character','1','--cpu','16']
        try:select.main()
        finally:sys.argv=old_argv
        for _ in range(300):
            if bottom.snapshot()['scene']==2:break
            time.sleep(.2)
        select.act(frames=150)
        for expanded in (0,1):
            set_word('mp_native_expanded',expanded);select.act(frames=60);grab(f'stage{stage}-wide{expanded}')
        if '--flash' in sys.argv:
            # Hold lbbgflash's full-screen quad (mode 5, colour xC) and capture.
            def flash_read():
                with socket.create_connection(('127.0.0.1',24689),3) as s:
                    s.settimeout(5);packet(s,'?');receive(s)
                    try:packet(s,f"m{symbols['lbl_80433658']:x},48");return receive(s)
                    finally:packet(s,'c');packet(s,'D');receive(s)
            before=flash_read();print('bgflash state',before,flush=True)
            for colour,name in ((0xffffffff,'white'),(0x000000ff,'black')):
                set_word('lbl_80433658',0x05000000,'big')
                with socket.create_connection(('127.0.0.1',24689),3) as s:
                    s.settimeout(5);packet(s,'?');receive(s)
                    try:packet(s,f"M{symbols['lbl_80433658']+12:x},4:"+colour.to_bytes(4,'big').hex());assert receive(s)=='OK'
                    finally:packet(s,'c');packet(s,'D');receive(s)
                select.act(frames=20);grab(f'stage{stage}-flash-{name}')
            with socket.create_connection(('127.0.0.1',24689),3) as s:
                s.settimeout(5);packet(s,'?');receive(s)
                try:packet(s,f"M{symbols['lbl_80433658']:x},10:"+before[:32]);assert receive(s)=='OK'
                finally:packet(s,'c');packet(s,'D');receive(s)
        if '--fixture' in sys.argv:
            subprocess.run([sys.executable,str(Path(__file__).with_name('stereo_fixture_check.py'))])
        if '--toggle' in sys.argv:
            name=sys.argv[sys.argv.index('--toggle')+1]
            for value in (1,0):set_word(name,value,'big');select.act(frames=90);grab(f'stage{stage}-{name}{value}')
        raise SystemExit(0)
    if '--mode' in sys.argv:
        mode=int(sys.argv[sys.argv.index('--mode')+1],0)
        with socket.create_connection(('127.0.0.1',24689),3) as sock:
            sock.settimeout(5);packet(sock,'?');receive(sock)
            try:
                # MAINLIB_POKE="0x527=2": bytes in the game-mode save block
                # (0x521 Classic round, 0x527 Adventure stage).
                for item in filter(None,os.environ.get('MAINLIB_POKE','').split(',')):
                    off,val=(int(v,0) for v in item.split('='))
                    packet(sock,f'm{symbols["gmMainLib_804D3EE0"]:x},4');base=int.from_bytes(bytes.fromhex(receive(sock)),'big')
                    packet(sock,f'M{base+off:x},1:{val:02x}');assert receive(sock)=='OK'
                sm=symbols['state_machine'];flag=symbols['gm_80479D58']
                for addr,data in ((symbols['mp_test_mode_override'],f'{mode:02x}'),(sm+0xC,'01'),(flag+0xC,'00000001')):
                    packet(sock,f'M{addr:x},{len(data)//2:x}:{data}');assert receive(sock)=='OK'
            finally:packet(sock,'c');packet(sock,'D');receive(sock)
        if '--toggle' in sys.argv:
            # Alternate a big-endian engine switch during one scene.
            name=sys.argv[sys.argv.index('--toggle')+1];start=time.monotonic();value=0;phase=0
            while time.monotonic()-start<float(os.environ.get('RUN_SECONDS','150')):
                state=bottom.snapshot()
                if state['engine_failed']:print('ENGINE FAILED',flush=True);break
                if state['scene']==2 and time.monotonic()-start>20:
                    value^=1;set_word(name,value,'big');print('set',name,value,'frame',state['engine_frames'],flush=True)
                    time.sleep(5);grab(f'toggle{phase}-{name}{value}');phase+=1;time.sleep(5)
                else:time.sleep(1)
            raise SystemExit(0)
        deadline=time.monotonic()+float(os.environ.get('RUN_SECONDS','420'));seen=None;shots=0;start=time.monotonic();due=start+6
        profiled=0;setdone=0
        presses=[(float(t),int(m,0)) for t,m in (x.split(':') for x in os.environ.get('MODE_PRESS','').split(',') if x)]
        while time.monotonic()<deadline:
            state=bottom.snapshot()
            if state['engine_failed']:print('ENGINE FAILED',state,flush=True);break
            if state['scene']!=seen:
                seen=state['scene'];print('scene',seen,'mode',state['mode'],'frame',state['engine_frames'],round(time.monotonic()-start,1),flush=True)
                scene_start=time.monotonic()
                due=time.monotonic()+4
                if seen==8 and os.environ.get('MODE_CHARACTER'):
                    # Character select: pick the icon for P1, then start.
                    select.act(frames=90);css=select.observe()
                    bottom.choose(css,0,int(os.environ['MODE_CHARACTER']));select.act(frames=30);select.act(0x1000,4)
                    print('chose character',flush=True)
            if time.monotonic()>=due and shots<16:
                grab(f'mode{shots}-s{seen}');shots+=1;due=time.monotonic()+float(os.environ.get('MODE_SHOT_INTERVAL','25'))
            if os.environ.get('MODE_PROFILE') and seen==2 and not profiled and time.monotonic()-scene_start>float(os.environ.get('MODE_PROFILE_DELAY','8')):
                profiled=1;sample_profile(float(os.environ['MODE_PROFILE']),label)
            # MODE_SET_AT="12:name=value": write a big-endian word N seconds into scene 2.
            if os.environ.get('MODE_SET_AT') and seen==2 and not setdone:
                at,assign=os.environ['MODE_SET_AT'].split(':');name,value=assign.split('=')
                if time.monotonic()-scene_start>float(at):setdone=1;set_word(name,int(value,0),'big');print('set',name,flush=True)
            # MODE_PRESS="8:0x100,11:0x100": button mask at seconds after start.
            while presses and time.monotonic()-start>=presses[0][0]:
                at,mask=presses.pop(0);select.act(mask,4);print('pressed',hex(mask),'at',round(time.monotonic()-start,1),flush=True)
            time.sleep(.2)
        raise SystemExit(0)
    enter(0,1);enter(0,6);select.act(0x100,3)
    for _ in range(70):
        state=select.observe()
        if 'hand' in state:break
        select.act(frames=15)
    else:raise AssertionError(state)
    bottom.choose(state,0,1)
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            packet(sock,f'm{symbols["gmMainLib_804D3EE0"]:x},4')
            base=int.from_bytes(bytes.fromhex(receive(sock)),'big')
            packet(sock,f'M{base+0x521:x},1:{round_number:02x}');assert receive(sock)=='OK'
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    select.observe((0x1000,4,0,0));deadline=time.monotonic()+(300 if round_number>10 else 120);shots=0;seen=None
    while time.monotonic()<deadline:
        state=bottom.snapshot();assert not state['engine_failed'],state
        if state['scene']!=seen:seen=state['scene'];print('scene',seen,'mode',state['mode'],'frame',state['engine_frames'],flush=True)
        if state['scene']==2:break
        if state['scene']==32 and shots<4:
            select.observe((0,1,0,0))
            time.sleep(1.5 if shots else 1.0)
            grab(f'splash{shots}');shots+=1
            if shots==4 and round_number<=10:select.observe((0x1000,2,0,0))
        time.sleep(.05)
    print('scene',bottom.snapshot()['scene'],flush=True)
    if play:
        time.sleep(play);grab('match')
    if '--profile' in sys.argv:
        import re,random
        seconds=float(sys.argv[sys.argv.index('--profile')+1]);raw=[]
        select.act(frames=60)
        with socket.create_connection(('127.0.0.1',24689),5) as s:
            s.settimeout(20);packet(s,'?');receive(s)
            packet(s,'qXfer:threads:read::0,fff');threads=re.findall(r'id="([0-9a-fA-F]+)"',receive(s))
            packet(s,'c');end=time.monotonic()+seconds
            while time.monotonic()<end:
                time.sleep(random.uniform(.02,.05))
                s.sendall(b'\x03');receive(s)
                for t in threads:
                    packet(s,'Hg'+t)
                    if receive(s)!='OK':continue
                    packet(s,'pf');pc=int.from_bytes(bytes.fromhex(receive(s)),'little')
                    packet(s,'pe');lr=int.from_bytes(bytes.fromhex(receive(s)),'little')
                    raw.append((t,pc,lr))
                packet(s,'c')
            s.sendall(b'\x03');receive(s);packet(s,'c');packet(s,'D');receive(s)
        (ROOT/'build/perf'/f'{label}-samples.txt').write_text('\n'.join(f'{t} {pc:08x} {lr:08x}' for t,pc,lr in raw))
        print('samples',len(raw),flush=True)
        raise SystemExit(0)
    if '--ko' in sys.argv:
        # Test shortcut: move every CPU fighter below the blast zone.
        select.act(frames=120)
        def ko():
            with socket.create_connection(('127.0.0.1',24689),3) as sock:
                sock.settimeout(5);packet(sock,'?');receive(sock)
                def read(a,n):packet(sock,f'm{a:x},{n:x}');return bytes.fromhex(receive(sock))
                def word(a):return int.from_bytes(read(a,4),'big')
                try:
                    head=word(symbols['HSD_GObjPLinkHead']);gobj=word(head+32);index=0
                    while gobj and index<8:
                        fp=word(gobj+0x2c)
                        if index>0:
                            packet(sock,f'M{fp+0xb4:x},4:'+struct.pack('>f',-2000.0).hex());receive(sock)
                        gobj=word(gobj+8);index+=1
                finally:packet(sock,'c');packet(sock,'D');receive(sock)
        for _ in range(20):ko();time.sleep(.1)
        start=time.monotonic();shot=0;seen=None
        while time.monotonic()-start<float(os.environ.get('RUN_SECONDS','60')):
            state=bottom.snapshot()
            if state['scene']!=seen:seen=state['scene'];print('scene',seen,'frame',state['engine_frames'],round(time.monotonic()-start,1),flush=True)
            if state['engine_failed']:print('ENGINE FAILED',flush=True);break
            if shot<int(os.environ.get('KO_SHOTS','12')):grab(f'clear{shot}');shot+=1
            if shot==int(os.environ.get('KO_START','0') or 99):select.observe((0x1000,3,0,0))
            time.sleep(float(os.environ.get('KO_INTERVAL','1.5')))
        raise SystemExit(0)
    if '--toggle' in sys.argv:
        name=sys.argv[sys.argv.index('--toggle')+1];start=time.monotonic();value=0;phase=0
        while time.monotonic()-start<float(os.environ.get('RUN_SECONDS','150')):
            state=bottom.snapshot()
            if state['engine_failed']:print('ENGINE FAILED',flush=True);break
            value^=1;set_word(name,value,'big');print('set',name,value,'frame',state['engine_frames'],flush=True)
            time.sleep(6);grab(f'toggle{phase}-{name}{value}');phase+=1;time.sleep(6)
finally:
    proc.kill();proc.wait()
    if (SD/'game.log').exists():shutil.copyfile(SD/'game.log',OUT/f'{label}.log')
