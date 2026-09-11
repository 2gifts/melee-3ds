"""Identify the caller emitting GX points; debugger observations only."""
import socket,struct,json
from gameplay_test import symbols
from gdb_probe import packet,receive
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    sock.settimeout(8);packet(sock,'?');receive(sock)
    address=symbols['GXBegin']
    packet(sock,f'Z0,{address:x},4');assert receive(sock)=='OK'
    try:
        for i in range(2500):
            packet(sock,'c');receive(sock);packet(sock,'g');r=struct.unpack('<16I',bytes.fromhex(receive(sock))[:64])
            if i<3:print([hex(r[j]) for j in (0,1,2,14,15)])
            if r[0]==0xb8:
                code=sorted((a,n) for n,a in symbols.items() if not n.startswith(('mp_be_fix_','$')))
                caller=max((a,n) for a,n in code if a<=r[14])
                print(json.dumps({'count':r[2],'format':r[1],'caller':caller,'lr':hex(r[14])}));break
            packet(sock,f'z0,{address:x},4');receive(sock);packet(sock,'s');receive(sock)
            packet(sock,f'Z0,{address:x},4');receive(sock)
        else:print('No point primitive encountered')
    finally:
        packet(sock,f'z0,{address:x},4');receive(sock);packet(sock,'c');packet(sock,'D');receive(sock)
