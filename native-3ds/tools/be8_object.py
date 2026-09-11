"""Embed ARM BE8 engine objects in a little-endian 3DS executable.

Only ELF metadata, instructions and linker-owned relocation addends are
converted. Game constants remain big-endian. A table identifies words the
little-endian loader relocates; startup swaps those once before entering BE8.
"""
import hashlib
import struct
from pathlib import Path

EH = '16sHHIIIIIHHHHHH'
SH = '10I'
SYM = 'IIIBBH'


def convert(source, output):
    raw = Path(source).read_bytes()
    header = list(struct.unpack_from('>' + EH, raw))
    if raw[:6] != b'\x7fELF\x01\x02' or header[1:3] != [1,40]:
        raise ValueError('Expected ELF32 ARM big-endian relocatable object')
    sections = [list(struct.unpack_from('>' + SH, raw, header[6] + 40*i))
                for i in range(header[12])]
    payload = [bytearray(raw[s[4]:s[4]+s[5]]) if s[1] != 8 else bytearray()
               for s in sections]
    shstrings = payload[header[13]]
    def string(data, offset):
        return bytes(data[offset:data.index(0,offset)]).decode()
    def name(i): return string(shstrings, sections[i][0])
    symidx = next(i for i,s in enumerate(sections) if s[1] == 2)
    symsec = sections[symidx]
    symstr = payload[symsec[6]]
    symbols = [list(struct.unpack_from('>' + SYM,payload[symidx],i))
               for i in range(0,len(payload[symidx]),16)]
    for sym in symbols:
        label = string(symstr,sym[0])
        if sym[5] == 0 and (label.startswith('__aeabi_') or label in ('memcpy','memmove','memset','memcmp')):
            sym[0] = len(symstr)
            symstr.extend(('mp_be_'+label).encode()+b'\0')
    for i,s in enumerate(sections):
        # Original static DMA buffers rely on 32-byte GameCube linker alignment.
        # Over-align individual writable data sections; member offsets stay intact.
        if s[1] in (1,8) and s[2]&3==3 and s[5]>=32:
            s[8]=max(s[8],32)
        if s[1] == 1 and s[2] & 4:
            mapping = sorted((v[1],string(symstr,v[0])[:2]) for v in symbols
                             if v[5] == i and string(symstr,v[0])[:2] in ('$a','$t','$d'))
            if not mapping or mapping[0][0] != 0: mapping.insert(0,(0,'$a'))
            mapping.append((len(payload[i]),'$d'))
            for (start,kind),(end,_) in zip(mapping,mapping[1:]):
                stride = {'$a':4,'$t':2,'$d':1}[kind]
                if (end-start)%stride: raise ValueError('Unaligned ARM mapping range')
                for off in range(start,end,stride):
                    payload[i][off:off+stride] = payload[i][off:off+stride][::-1]
        elif s[1] == 0x70000003: # ARM attributes contain endian-specific lengths.
            # The outer application's native objects supply validated CPU/ABI attributes.
            payload[i] = bytearray(b'A')
            s[5] = 1
    fixups = []
    tag = hashlib.sha256(str(source).encode()).hexdigest()[:16]
    for i,s in enumerate(sections):
        if s[1] != 9: continue
        rels = [struct.unpack_from('>II',payload[i],n) for n in range(0,len(payload[i]),8)]
        target = s[7]
        for offset,info in rels:
            kind = info & 255
            if kind in (2,3,38,41) and not name(target).startswith('.debug'):
                # ARM32 REL relocations store their addend in the relocated word.
                payload[target][offset:offset+4] = payload[target][offset:offset+4][::-1]
                symbol_name = f'mp_be_fix_{tag}_{target}_{offset}'.encode()+b'\0'
                symbols.append([len(symstr),offset,4,0x11,0,target])
                symstr.extend(symbol_name)
                fixups.append(len(symbols)-1)
            elif kind in (2,3,38,41,42) and name(target).startswith(('.debug','.ARM.exidx')):
                payload[target][offset:offset+4] = payload[target][offset:offset+4][::-1]
        payload[i] = bytearray(b''.join(struct.pack('<II',*r) for r in rels))
    payload[symidx] = bytearray(b''.join(struct.pack('<'+SYM,*s) for s in symbols))
    def add_section(label,typ,flags,data,link=0,info=0,align=4,entsize=0):
        offset = len(shstrings)
        shstrings.extend(label.encode()+b'\0')
        sections.append([offset,typ,flags,0,0,len(data),link,info,align,entsize])
        payload.append(bytearray(data))
        return len(sections)-1
    # The two-pass linker collects only fixup symbols in reachable sections.
    # A table attached here would keep otherwise unused engine functions alive.
    result = bytearray(52)
    for s,data in zip(sections,payload):
        if s[1] == 0: continue
        align = max(1,s[8])
        result.extend(b'\0'*((-len(result))%align))
        s[4] = len(result)
        if s[1] != 8:
            s[5] = len(data)
            result.extend(data)
    result.extend(b'\0'*((-len(result))%4))
    header[6] = len(result)
    header[12] = len(sections)
    header[0] = raw[:5]+b'\x01'+raw[6:16]
    header[7] &= ~0x00800000
    for s in sections: result.extend(struct.pack('<'+SH,*s))
    struct.pack_into('<'+EH,result,0,*header)
    Path(output).write_bytes(result)
    return len(fixups)
