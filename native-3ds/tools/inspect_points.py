import json,socket,struct
from gameplay_test import symbols,ROOT
from gdb_probe import packet,receive
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    sock.settimeout(5);packet(sock,'?');receive(sock)
    try:
        packet(sock,f'm{symbols["point_list"]:x},14');meta=struct.unpack('>5I',bytes.fromhex(receive(sock)))
        print(json.dumps({'list_address':hex(meta[0]),'bytes':meta[1],'offset':meta[2],'count':meta[3],'stride':meta[4]}))
        if meta[0]:
            offset=max(0,meta[2]-32);length=min(meta[1]-offset,160)
            packet(sock,f'm{meta[0]+offset:x},{length:x}');neighborhood=bytes.fromhex(receive(sock));print('header neighborhood:',neighborhood.hex())
            payload=neighborhood[meta[2]-offset+3:meta[2]-offset+27]
            original=(ROOT/'assets/GALE01/files/GrIz.dat').read_bytes();found=original.find(payload)
            if found>=3:print('Original GrIz.dat opcode:',original[found-3:found].hex(),'offset',hex(found-3))
        packet(sock,f'm{symbols["point_callers"]:x},80');raw=bytes.fromhex(receive(sock))
        code=sorted((a,n) for n,a in symbols.items() if not n.startswith(('mp_be_fix_','$')))
        for address,count in struct.iter_unpack('>II',raw):
            if not address:continue
            base,name=max((a,n) for a,n in code if a<=address)
            print(json.dumps({'caller':name,'offset':hex(address-base),'address':hex(address),'vertices':count}))
    finally:packet(sock,'c');packet(sock,'D');receive(sock)
