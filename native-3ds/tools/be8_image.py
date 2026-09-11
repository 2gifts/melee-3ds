"""Resolve BE8 relocation words before packaging a fixed-layout 3DS image.

Modern Luma homebrew loads the three page-aligned segments at 0x100000.
Native relocations remain standard 3DSX relocations; BE8 words are finalized
here and excluded from the little-endian loader's relocation records.
Startup checks the actual segment addresses before entering engine code.
"""
import json,struct
from pathlib import Path

class ElfImage:
    def __init__(self,data):
        self.data=bytearray(data)
        self.header=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
        assert data[:6]==b'\x7fELF\x01\x01' and self.header[1:3]==(2,40),'Expected linked ARM ELF32'
        self.sections=[list(struct.unpack_from('<10I',data,self.header[6]+40*i)) for i in range(self.header[12])]
        self.segments=[struct.unpack_from('<8I',data,self.header[5]+32*i) for i in range(self.header[10])]
        self.loads=[p for p in self.segments if p[0]==1]
        self.symbols={}
        for s in self.sections:
            if s[1]!=2:continue
            strings=self.sections[s[6]];names=data[strings[4]:strings[4]+strings[5]]
            for offset in range(s[4],s[4]+s[5],16):
                name,value,size,info,other,section=struct.unpack_from('<IIIBBH',data,offset)
                label=names[name:names.index(0,name)].decode()
                if section:self.symbols[label]=(value,size,section)
    def offset(self,address,size=4):
        for p in self.loads:
            if p[2]<=address and address+size<=p[2]+p[4]:return p[1]+address-p[2]
        raise ValueError(f'Address {address:08x} has no initialized image bytes')

def prepare(source,destination):
    image=ElfImage(Path(source).read_bytes())
    assert len(image.loads)==3 and [p[6] for p in image.loads]==[5,4,6]
    addresses=[p[2] for p in image.loads]
    assert addresses[0]==0x100000
    for a,b in zip(image.loads,image.loads[1:]):assert b[2]==(a[2]+a[5]+4095)&~4095,'Non-contiguous image layout'
    words={value for name,(value,size,section) in image.symbols.items() if name.startswith('mp_be_fix_')}
    assert words,'Missing BE8 relocation markers'
    removed=set();counts={str(p[6]):0 for p in image.loads}
    for i,s in enumerate(image.sections):
        if s[1]!=9 or not image.sections[s[7]][2]&2:continue
        remaining=[]
        for off in range(s[4],s[4]+s[5],8):
            address,info=struct.unpack_from('<II',image.data,off)
            if address in words:
                assert info&255 in (2,3,38,41),(hex(address),info&255)
                removed.add(address)
            else:remaining.append(bytes(image.data[off:off+8]))
        payload=b''.join(remaining);image.data[s[4]:s[4]+s[5]]=payload+b'\0'*(s[5]-len(payload))
        s[5]=len(payload);struct.pack_into('<10I',image.data,image.header[6]+40*i,*s)
    assert removed==words,('Relocation marker mismatch',len(words-removed))
    for address in words:
        assert address%4==0
        offset=image.offset(address);image.data[offset:offset+4]=image.data[offset:offset+4][::-1]
        p=next(p for p in image.loads if p[2]<=address<p[2]+p[4]);counts[str(p[6])]+=1
    layout=image.offset(image.symbols['mp_expected_image_layout'][0],16)
    assert struct.unpack_from('<4I',image.data,layout)==(0x4d50494d,0,0,0),'Image already finalized'
    struct.pack_into('<4I',image.data,layout,0x4d50494d,*addresses)
    Path(destination).write_bytes(image.data)
    result={'layout':'fixed addresses checked at startup','segment_addresses':[hex(a) for a in addresses],
            'be8_words':len(words),'words_by_segment_permissions':counts,'runtime_code_writes':0}
    Path(destination).with_suffix('.image.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
