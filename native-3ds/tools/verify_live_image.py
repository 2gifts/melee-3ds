"""Check that gameplay has not changed protected code or read-only data."""
import json,socket
from be8_image import ElfImage
from gameplay_test import ROOT,symbols,packet,receive

def main():
    image=ElfImage((ROOT/'build/game/melee.elf').read_bytes());mismatches=[];checked=0
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(8);packet(sock,'?');receive(sock)
        try:
            packet(sock,f'm{symbols["engine_failed"]:x},4');assert receive(sock)=='00000000'
            for p in image.loads:
                if p[6]&2:continue
                # The launcher supplies the native CRT's initial parameter
                # block in the first 128 bytes before protecting the image.
                begin=128 if p[6]&4 else 0
                for offset in range(begin,p[4],512):
                    size=min(512,p[4]-offset);address=p[2]+offset
                    packet(sock,f'm{address:x},{size:x}');actual=bytes.fromhex(receive(sock))
                    expected=image.data[p[1]+offset:p[1]+offset+size]
                    if actual!=expected:mismatches.extend(hex(address+i) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b)
                    checked+=size
                print(f'Checked protected segment {p[2]:08x}: {p[4]-begin} bytes',flush=True)
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    result={'checked_bytes':checked,'changed_bytes':len(mismatches),'first_changed_addresses':mismatches[:32]}
    (ROOT/'build/live-image-verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    assert not mismatches,'Gameplay wrote protected code or read-only data'
if __name__=='__main__':main()
