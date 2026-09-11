"""Read menu/controller state from the local native-engine test process."""
import json,socket,struct,subprocess
from gdb_probe import ROOT,packet,receive
listing=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),str(ROOT/'build/game/melee.elf')],text=True)
symbols={p[2]:int(p[0],16) for line in listing.splitlines() if len(p:=line.split())==3}
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    sock.settimeout(5);packet(sock,'?');receive(sock)
    def read(address,size):
        packet(sock,f'm{address:x},{size:x}');return bytes.fromhex(receive(sock))
    try:
        result={}
        cursor=struct.unpack('>I',read(symbols['mnCharSel_804A0BC0'],4))[0]
        if cursor>=0x08000000:
            data=read(cursor,20);result['cursor']={'state':data[5],'held_door':data[6],'x':struct.unpack('>f',data[12:16])[0],'y':struct.unpack('>f',data[16:20])[0]}
            result['doors']=[]
            for i in range(2):
                door=read(symbols['mnCharSel_803F0DFC']+i*36,36)
                result['doors'].append({'kind':door[11],'icon':door[14],'selected':door[9]})
        for name in ('mnStageSel_804D6CAE','mnStageSel_804D6CAF','engine_frames','engine_failed'):
            data=read(symbols[name],1 if name.startswith('mnStage') else 4)
            result[name]=int.from_bytes(data,'big' if name.startswith('mn') else 'little')
        head=int.from_bytes(read(symbols['HSD_GObjPLinkHead'],4),'big')
        if head>=0x08000000:
            gobj=int.from_bytes(read(head+8*4,4),'big');fighters=[]
            while gobj and len(fighters)<8:
                data=read(gobj,0x34);fp=int.from_bytes(data[0x2c:0x30],'big');f=read(fp,0xbC)
                kind=int.from_bytes(f[4:8],'big');motion=int.from_bytes(f[0x10:0x14],'big')
                if kind<33 and motion<1000:
                    fighters.append({'address':hex(fp),'kind':kind,'state':motion,'position':struct.unpack('>fff',f[0xb0:0xbc]),'damage_percent':struct.unpack('>f',read(fp+0x1830,4))[0]})
                gobj=int.from_bytes(data[8:12],'big')
            result['fighters']=fighters
        print(json.dumps(result,indent=2))
    finally:packet(sock,'c');packet(sock,'D');receive(sock)
