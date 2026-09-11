"""Select a stage through controller input, using read-only menu observations."""
import argparse,json,math,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive

def observe(control=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        # Snapshot aligned 512-byte blocks while paused. CSS's icons, doors
        # and linked objects otherwise cause hundreds of tiny round trips.
        blocks={}
        def read(a,n):
            result=b''
            while n:
                base=a&~511;offset=a-base;size=min(n,512-offset)
                if base not in blocks:
                    packet(sock,f'm{base:x},200');blocks[base]=bytes.fromhex(receive(sock))
                    if len(blocks[base])!=512:raise RuntimeError(f'Invalid menu block {base:x}')
                result+=blocks[base][offset:offset+size];a+=size;n-=size
            return result
        def word(a,endian='big'):return int.from_bytes(read(a,4),endian)
        def xyz(a):return struct.unpack('>fff',read(a,12))
        try:
            if control:
                if 'mp_test_frame_limit' in symbols:
                    packet(sock,f'M{symbols["mp_test_frame_limit"]:x},4:00000000');assert receive(sock)=='OK'
                data=struct.pack('<IIIii',time.time_ns()&0xffffffff,*control)
                packet(sock,f'M{symbols["mp_test_control"]:x},{len(data):x}:'+data.hex());assert receive(sock)=='OK'
            state={'frame':word(symbols['engine_frames'],'little'),'failed':word(symbols['engine_failed'],'little')}
            if state['frame']<30:return state
            head=word(symbols['HSD_GObjPLinkHead']);gobjs=[]
            if not head:return state
            for link in (5,7):
                gobj=word(head+link*4)
                for _ in range(100):
                    if not gobj:break
                    gobjs.append(gobj);gobj=word(gobj+8)
            for gobj in gobjs:
                proc=word(gobj+0x18)
                for __ in range(10):
                    if not proc:break
                    if word(proc+0x14)==symbols['fn_8025A310']:
                        joint=word(gobj+0x28);m=struct.unpack('>12f',read(joint+0x44,48))
                        state['cursor']=(m[3],m[7]);state['local']=xyz(joint+0x38)
                    if word(proc+0x14)==symbols['mnCharSel_CursorThink']:
                        cursor=read(word(gobj+0x2c),20)
                        if cursor[4]==0:
                            state['hand']={'state':cursor[5],'door':cursor[6],
                                           'xy':struct.unpack('>ff',cursor[12:20])}
                    if word(proc+0x14)==symbols['fn_8022AFEC']:
                        menu_data=word(gobj+0x2c)
                        menu=read(menu_data,4) if menu_data else b''
                        if len(menu)==4 and menu[0]==read(symbols['mn_804A04F0'],1)[0]:
                            state['menu']={'kind':menu[0],'selection':menu[1],'state':menu[2]}
                    proc=word(proc)
            if 'hand' in state:
                state['doors']=[]
                for i in range(2):
                    door=read(symbols['mnCharSel_803F0DFC']+36*i,36)
                    token_pointer=word(symbols['mnCharSel_804A0BD0']+4*i)
                    if not token_pointer:
                        # The hand's proc is linked before token creation
                        # finishes during a CSS transition.
                        state.pop('hand',None);return state
                    token=read(token_pointer,24)
                    state['doors'].append({'kind':door[11],'icon':door[14],
                        'selected':door[9],'held':token[5],'xy':struct.unpack('>ff',token[8:16]),
                        'toggle':struct.unpack('>ff',door[20:28])})
                state['characters']=[]
                for i in range(25):
                    icon=read(symbols['icons']+28*i,28)
                    left,right,top,bottom=struct.unpack('>ffff',icon[12:28])
                    state['characters'].append({'id':i,'available':icon[2],
                                                'xy':((left+right)/2,(top+bottom)/2)})
            if 'cursor' in state:
                state['selection']=read(symbols['mnStageSel_804D6CAE'],1)[0]
                state['phase']=read(symbols['mnStageSel_804D6CAF'],1)[0]
                state['icons']=[]
                for i in range(29):
                    row=symbols['mnStageSel_803F06D0']+i*28;joint=word(row)
                    m=struct.unpack('>12f',read(joint+0x44,48))
                    state['icons'].append({'id':i,'available':read(row+8,1)[0],
                        'stage_kind':read(row+11,1)[0],'xy':(m[3],m[7])})
            return state
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def progress():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            result={}
            for key,name in [('frame','engine_frames'),('failed','engine_failed')]:
                packet(sock,f'm{symbols[name]:x},4');result[key]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def act(buttons=0,frames=2,x=0,y=0):
    s=observe((buttons,frames,x,y));target=s['frame']+frames+1
    deadline=time.monotonic()+120
    while s['frame']<target:
        if s['failed']:raise RuntimeError(s)
        if time.monotonic()>deadline:raise TimeoutError('Controller input did not advance')
        # Give long held/released inputs time to execute without repeatedly
        # stopping the emulator. Short cursor taps still get a short wait.
        time.sleep(min(.5,max(.02,(target-s['frame'])/60)));s=progress()
    return observe()


def move_hand(s,target):
    for _ in range(120):
        if 'hand' not in s:raise RuntimeError('Character selection closed while moving its hand')
        dx,dy=(target[i]-s['hand']['xy'][i] for i in range(2))
        if max(abs(dx),abs(dy))<.6:
            s=act(frames=8)
            if max(abs(target[i]-s['hand']['xy'][i]) for i in range(2))<.6:return s
            continue
        # CSS has a quadratic stick response. Observe after each bounded input,
        # including the original PAD clamping, rather than timing a long sweep.
        axis=0 if abs(dx)>abs(dy) else 1;delta=(dx,dy)[axis]
        value=round(math.copysign(min(80,math.sqrt(200+abs(delta)/.0002)),delta))
        s=act(frames=1,x=value if axis==0 else 0,y=value if axis else 0)
    raise RuntimeError('Character hand failed to converge')


def select_character(s,door,icon):
    target=s['characters'][icon]
    if not target['available']:raise RuntimeError('Requested character is locked')
    existing=s['doors'][door]
    if existing['icon']==icon and existing['selected'] and not existing['held'] and existing['kind']==(0 if door==0 else 1):
        return s
    if s['hand']['state']==1 and s['hand']['door']!=door:
        s=act(0x100);s=act()
    if door==0 and s['doors'][0]['kind']==1:
        bounds=s['doors'][0]['toggle'];s=move_hand(s,((bounds[0]+bounds[1])/2,-2.2))
        s=act(0x100,1);s=act(frames=5)
    if door!=0 and s['doors'][door]['kind']==3:
        bounds=s['doors'][door]['toggle'];s=move_hand(s,((bounds[0]+bounds[1])/2,-2.2))
        s=act(0x100,1);s=act(frames=5)
        if s['doors'][door]['kind']!=(0 if door==0 else 1):raise RuntimeError('Fighter door did not open')
    if s['hand']['state']!=1:
        if s['doors'][door]['icon']>=25 and door==0:
            s=move_hand(s,(-20,16))
        else:
            token=s['doors'][door]['xy']
            s=move_hand(s,(token[0]-3.8,token[1]+2.6))
            s=act(0x100);s=act()
    if s['hand']['state']!=1 or s['hand']['door']!=door:
        raise RuntimeError(f'Could not pick up character token: {s["hand"]}')
    s=move_hand(s,(target['xy'][0]-2.7,target['xy'][1]+2))
    s=act(0x100);s=act()
    if s['doors'][door]['icon']!=icon or s['doors'][door]['held'] or s['doors'][door]['kind']!=(0 if door==0 else 1):
        raise RuntimeError(f'Character selection failed: {s["doors"]}')
    print(json.dumps({'selected_character':icon,'door':door,'frame':s['frame']}),flush=True)
    return s


def open_mode(s,versus):
    s=act()
    for _ in range(24):
        if 'hand' in s:return s
        if 'menu' not in s or s['menu']['state']!=0:s=act(frames=12);continue
        m=s['menu'];print(json.dumps({'menu':m}),flush=True)
        if m['kind']==0:
            desired=1 if versus else 0
            s=act(0x100 if m['selection']==desired else 4 if m['selection']<desired else 8)
        elif m['kind']==(2 if versus else 1):
            desired=0 if versus else 4
            s=act(0x100 if m['selection']==desired else 4 if m['selection']<desired else 8)
        else:s=act(0x200)
        s=act(frames=30)
    raise RuntimeError('Requested game mode did not open')

def open_versus(s):return open_mode(s,True)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('stage',type=int,default=8,nargs='?')
    ap.add_argument('--character',type=int,default=1);ap.add_argument('--cpu',type=int,default=1)
    ap.add_argument('--versus',action='store_true')
    ap.add_argument('--fresh',action='store_true',help='Drive a fresh boot with prompts already skipped')
    ap.add_argument('--stop-at-stage-selection',action='store_true',help='Position cursor and leave final confirmation to a debugger')
    args=ap.parse_args();deadline=time.monotonic()+900;stopped=False;versus_open=False
    while time.monotonic()<deadline:
        try:s=observe()
        except ConnectionRefusedError:time.sleep(.1);continue
        if s['failed']:raise RuntimeError(s)
        if args.fresh and not stopped and not versus_open:
            # Disable the historical fixed-timing memory-card script. Title
            # input is controller-only and bounded; menu state drives the rest.
            if s['frame']<60:s=act(frames=10)
            elif 'menu' not in s and 'hand' not in s:
                s=act(0x1000,2);s=act(frames=30)
        if not versus_open and (s['frame']>=450 or args.fresh) and 'menu' in s:
            s=open_mode(s,args.versus);versus_open=True
        if 'hand' in s and not stopped:
            s=act();stopped=True
            s=select_character(s,0,args.character)
            s=select_character(s,1,args.cpu)
            s=act(0x1000,4);s=act()
        if 'cursor' in s and s['phase']==0:break
        time.sleep(.1)
    else:raise TimeoutError('Stage selection did not appear')
    target=s['icons'][args.stage]
    print(json.dumps({'target':target,'cursor':s['cursor'],'selection':s['selection']}),flush=True)
    if target['available']!=2:raise RuntimeError('Requested test stage is locked')
    for attempt in range(100):
        if s['selection']==args.stage:
            # PAD queues can still contain the last cursor movement. Drain
            # them and verify the actual highlighted icon before pressing A.
            s=act(frames=12)
            if s['selection']!=args.stage:continue
            if args.stop_at_stage_selection:return
            observe((0x100,4,0,0));print('Selected stage through A input',flush=True)
            from gameplay_test import exchange
            match_deadline=time.monotonic()+90;retry=time.monotonic()+3
            while time.monotonic()<match_deadline:
                state=exchange()
                if state['failed']:raise RuntimeError(state)
                if len(state['fighters'])>=2:
                    assert state['stage_kind']==target['stage_kind'],(target,state)
                    print(json.dumps({'verified_stage_kind':state['stage_kind']}),flush=True)
                    return
                if time.monotonic()>=retry:
                    waiting=observe()
                    # Some stage icons finish their entrance animation after
                    # the cursor exists. Retry confirmation only while still
                    # on the same selected icon; never send it into gameplay.
                    if waiting.get('selection')==args.stage and 'cursor' in waiting:
                        act(frames=2);observe((0x100,4,0,0))
                    retry=time.monotonic()+3
                time.sleep(.1)
            raise TimeoutError(f'Selected match did not start: {observe()}')
        dx=target['xy'][0]-s['cursor'][0];dy=target['xy'][1]-s['cursor'][1]
        x=max(-80,min(80,round(dx/.03)));y=max(-80,min(80,round(dy/.03)))
        # A one-render-frame pulse can land between the original game's pad
        # polls. If it made no progress, hold the same real input for two
        # frames; do not alter the cursor or stage-selection memory.
        old_cursor=s['cursor']
        start=observe((0,2 if attempt and old_cursor==previous_cursor else 1,x,y))['frame']
        previous_cursor=old_cursor
        while s['frame']<=start+1:
            time.sleep(.02);s=progress()
            if s['failed']:raise RuntimeError(s)
        s=observe()
        if 'cursor' not in s:raise RuntimeError(s)
        print(json.dumps({'frame':s['frame'],'cursor':s['cursor'],'selection':s['selection']}),flush=True)
    raise RuntimeError('Stage cursor failed to converge')

if __name__=='__main__':main()
