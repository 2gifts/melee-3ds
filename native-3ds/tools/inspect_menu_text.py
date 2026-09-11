"""Read native-engine text buffers without changing the menu."""
import json,socket,struct
from gameplay_test import symbols,packet,receive,ROOT
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    packet(sock,'?');receive(sock)
    def read(a,n):
        chunks=[]
        while n:
            take=min(n,512);packet(sock,f'm{a:x},{take:x}');chunks.append(bytes.fromhex(receive(sock)));a+=take;n-=take
        return b''.join(chunks)
    def word(a):return int.from_bytes(read(a,4),'big')
    try:
        result={'name':read(symbols['mnNameNew_CurrentNameText'],16).hex(),'texts':[]}
        text=word(symbols['HSD_SisLib_804D7978'])
        for i in range(160):
            if not text:break
            raw=read(text,160);sis=int.from_bytes(raw[0x5c:0x60],'big');alloc=int.from_bytes(raw[0x64:0x68],'big')
            if alloc:
                result['texts'].append({'pointer':hex(text),'pos':struct.unpack('>3f',raw[:12]),'font':struct.unpack('>2f',raw[0x24:0x2c]),
                    'color':raw[0x30:0x34].hex(),'hidden':raw[0x4d],'sis':read(sis,100).hex() if sis else None,'width':struct.unpack('>f',raw[0x70:0x74])[0]})
            text=int.from_bytes(raw[0x50:0x54],'big')
        (ROOT/'build/menu-text-inspection.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:packet(sock,'c');packet(sock,'D');receive(sock)
