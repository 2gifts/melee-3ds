"""Read the largest unsupported vertex material captured by the renderer."""
import json,socket,struct
from gameplay_test import ROOT,symbols,packet,receive

def main():
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s)
        def read(name,n):
            address=symbols[name];data=b''
            while n:
                k=min(n,512);packet(s,f'm{address:x},{k:x}');data+=bytes.fromhex(receive(s));address+=k;n-=k
            return data
        try:
            result={name:int.from_bytes(read('mp_fallback_'+name,4),'big') for name in ('vertex_count','reason','stages')}
            raw=read('mp_fallback_tev',1920);result['tev']=[struct.unpack('>30I',raw[i*120:(i+1)*120]) for i in range(result['stages'])]
            for name in ('colors','konst'):result[name]=list(read('mp_fallback_'+name,16))
            result['alpha']=struct.unpack('>2f',read('mp_fallback_alpha',8))
        finally:packet(s,'c');packet(s,'D');receive(s)
    (ROOT/'build/fallback-material.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
