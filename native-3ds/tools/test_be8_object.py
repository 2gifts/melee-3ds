"""Exercise ELF32 extended numbering at the BE8 conversion boundary.

Reference: https://gabi.xinuos.com/elf/03-sheader.html and 05-symtab.html.
The fixture contains real high-index code/data, mixed ARM/Thumb/literal mapping,
ordinary big-endian constants, a pointer relocation, and reserved symbol indices.
"""
import json
import struct
import subprocess
from pathlib import Path
from be8_object import convert, EH, SH, SYM, SHN_LORESERVE, SHN_XINDEX
from build import ROOT, local_clang


def fixture(path, existing_index_table):
    text, data, names, syms, strings, rel, indices = range(SHN_LORESERVE, SHN_LORESERVE+7)
    count = indices+int(existing_index_table)
    sections = [[0]*10 for _ in range(count)]
    payloads = [b'']*count
    shstr = bytearray(b'\0'); ststr = bytearray(b'\0')
    def add(i, name, kind, flags, payload, link=0, info=0, align=4, entsize=0):
        off = len(shstr); shstr.extend(name.encode()+b'\0')
        sections[i] = [off,kind,flags,0,0,len(payload),link,info,align,entsize]
        payloads[i] = payload
    symbols = [[0]*6]; x = [0]
    def symbol(name, value, size, info, section):
        off = len(ststr); ststr.extend(name.encode()+b'\0')
        symbols.append([off,value,size,info,0,section]); x.append(0)
    if existing_index_table:
        for label, address in [('$a',0),('$d',4),('$t',8)]:
            symbol(label,address,0,0,SHN_XINDEX); x[-1] = text
        symbol('constant',0,4,1,SHN_XINDEX); x[-1] = data
    local_end = len(symbols)
    memcpy = len(symbols); symbol('memcpy',0,0,0x10,0)
    symbol('absolute',0x10203040,0,0x10,0xfff1)
    symbol('common',4,8,0x11,0xfff2)
    code = bytes.fromhex('e3a0002a11223344bf004770') if existing_index_table else bytes.fromhex('e3a0002a')
    add(text,'.text.high',1,6,code)
    add(data,'.data.high',1,3,bytes.fromhex('1234567800000004')+bytes(24))
    add(names,'.shstrtab',3,0,b'',align=1)
    add(syms,'.symtab',2,0,b''.join(struct.pack('>'+SYM,*s) for s in symbols),strings,local_end,entsize=16)
    add(strings,'.strtab',3,0,ststr,align=1)
    add(rel,'.rel.data.high',9,0,struct.pack('>II',4,(memcpy<<8)|2),syms,data,entsize=8)
    if existing_index_table:
        add(indices,'.symtab_shndx',18,0,struct.pack('>'+str(len(x))+'I',*x),syms,entsize=4)
    payloads[names] = shstr
    raw = bytearray(52)
    for s,p in zip(sections,payloads):
        if not s[1]: continue
        raw.extend(bytes((-len(raw))%max(s[8],1)))
        s[4] = len(raw); s[5] = len(p); raw.extend(p)
    raw.extend(bytes((-len(raw))%4)); shoff = len(raw)
    sections[0][5] = count; sections[0][6] = names
    for s in sections: raw.extend(struct.pack('>'+SH,*s))
    ident = b'\x7fELF\x01\x02\x01'+bytes(9)
    struct.pack_into('>'+EH,raw,0,ident,1,40,1,0,0,shoff,0x05000000,52,0,0,40,0,SHN_XINDEX)
    path.write_bytes(raw)
    return text,data,names,syms,strings,memcpy


def check(path, meta, existing):
    raw = path.read_bytes(); h = struct.unpack_from('<'+EH,raw)
    assert raw[:6] == b'\x7fELF\x01\x01' and h[12] == 0 and h[13] == SHN_XINDEX
    s0 = struct.unpack_from('<'+SH,raw,h[6]); n = s0[5]
    sections = [struct.unpack_from('<'+SH,raw,h[6]+40*i) for i in range(n)]
    def contents(i):
        s = sections[i]; return raw[s[4]:s[4]+s[5]]
    text,data,names,syms,strings,memcpy = meta
    assert s0[6] == names and s0[4] == 0
    assert contents(text) == (bytes.fromhex('2a00a0e31122334400bf7047') if existing else bytes.fromhex('2a00a0e3'))
    assert contents(data) == bytes.fromhex('1234567804000000')+bytes(24)
    assert sections[data][8] == 32
    symbols = list(struct.iter_unpack('<'+SYM,contents(syms)))
    strings = contents(strings)
    def label(s): return strings[s[0]:strings.index(0,s[0])].decode()
    assert label(symbols[memcpy]) == 'mp_be_memcpy'
    assert next(s for s in symbols if label(s)=='absolute')[5] == 0xfff1
    assert next(s for s in symbols if label(s)=='common')[5] == 0xfff2
    index = next(i for i,s in enumerate(sections) if s[1] == 18)
    assert sections[index][6] == syms and sections[index][9] == 4
    extended = [x[0] for x in struct.iter_unpack('<I',contents(index))]
    assert len(extended) == len(symbols)
    assert symbols[-1][5] == SHN_XINDEX and extended[-1] == data
    assert label(symbols[-1]).startswith('mp_be_fix_') and symbols[-1][1:3] == (4,4)
    if existing:
        assert extended[1:5] == [text,text,text,data]
    assert all(x == 0 for s,x in zip(symbols,extended) if s[5] != SHN_XINDEX)
    return dict(sections=n,symbols=len(symbols),existing_index_table=existing,passed=True)


def main():
    out = ROOT/'build/be8-object-test'; out.mkdir(parents=True,exist_ok=True)
    llvm = Path(local_clang()).parent
    report = []
    for existing in (False,True):
        stem = out/('existing' if existing else 'created')
        source = stem.with_suffix('.raw.o'); target = stem.with_suffix('.o')
        meta = fixture(source,existing)
        assert convert(source,target) == 1
        result = check(target,meta,existing)
        # LLVM independently reads the metadata and resolves high-index symbols.
        subprocess.run([str(llvm/'ld.lld.exe'),'-r','-m','armelf',str(target),'-o',str(stem.with_suffix('.linked.o'))],check=True,capture_output=True)
        output = subprocess.check_output([str(llvm/'llvm-nm.exe'),str(target)],text=True)
        assert ' U mp_be_memcpy' in output and ' D mp_be_fix_' in output
        result['llvm_relink_passed'] = True; report.append(result)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__': main()
