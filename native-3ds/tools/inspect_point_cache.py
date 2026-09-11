"""Read cache keys for the most recently decoded GX point display list."""
import json,socket,struct
from gameplay_test import ROOT,symbols,packet,receive
def inspect():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):
            data=bytearray()
            while n:
                size=min(n,512);packet(sock,f'm{a:x},{size:x}');part=bytes.fromhex(receive(sock))
                if len(part)!=size:raise RuntimeError(f'Invalid cache address {a:x}, {size} bytes')
                data+=part;a+=size;n-=size
            return bytes(data)
        def words(b):return struct.unpack('>'+str(len(b)//4)+'I',b)
        try:
            point=words(read(symbols['point_list'],20));pointer=point[0];slot=((pointer>>5)^(pointer>>14))&255
            result={'point_list':point,'entries':[]}
            for i in range(8):
                data=read(symbols['geometry_cache']+(slot*8+i)*480,480)
                if words(data[:4])[0]!=pointer:continue
                k=data[12:360];sources=[words(data[360+j*16:376+j*16]) for j in range(7)]
                result['entries'].append({'header':words(data[:12]),'key_words':words(k),
                    'sources':sources,'source_changed':[bool(n and read(src,n)!=read(copy,n)) if src and copy else None for src,copy,storage,n in sources],
                    'first_last':words(data[472:480])})
            result['frame']=int.from_bytes(read(symbols['engine_frames'],4),'little')
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    (ROOT/'build/point-cache-inspection.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':inspect()
