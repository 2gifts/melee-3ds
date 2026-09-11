"""Replay 3DSX relocation and enforce hardware RX/R/RW segment permissions."""
import argparse,json,struct
from pathlib import Path
from be8_image import ElfImage
from build import ROOT

def load_3dsx(path,base=0x100000):
    raw=Path(path).read_bytes()
    magic,header,rel_header,version,flags,code,rodata,data,bss=struct.unpack_from('<4sHH6I',raw)
    assert (magic,header,rel_header,version,flags)==(b'3DSX',32,8,0,0)
    sizes=[code,rodata,data];rounded=[(n+4095)&~4095 for n in sizes]
    starts=[0,rounded[0],rounded[0]+rounded[1]]
    memory=bytearray(sum(rounded));counts=struct.unpack_from('<6I',raw,header);cursor=header+24
    for start,size in zip(starts,[code,rodata,data-bss]):memory[start:start+size]=raw[cursor:cursor+size];cursor+=size
    relocated=set()
    for segment in range(3):
        for kind in range(2):
            word=0
            for _ in range(counts[2*segment+kind]):
                skip,patch=struct.unpack_from('<HH',raw,cursor);cursor+=4;word+=skip
                for _ in range(patch):
                    position=starts[segment]+4*word;assert position+4<=starts[segment]+rounded[segment]
                    encoded=struct.unpack_from('<I',memory,position)[0];subtype=encoded>>28
                    # Luma's segments are contiguous and page aligned, so its
                    # TranslateAddr(offset) is exactly base + offset here.
                    address=base+(encoded&0x0fffffff);assert base<=address<=base+len(memory)
                    if kind==0:assert subtype==0;value=address
                    else:
                        assert subtype in (0,1);value=(address-(base+position))&0xffffffff
                        if subtype==1:value&=0x7fffffff
                    struct.pack_into('<I',memory,position,value);relocated.add(base+position);word+=1
    assert cursor==len(raw)
    return memory,[base+s for s in starts],rounded,relocated

def check(linked,built,binary):
    original=ElfImage(Path(linked).read_bytes());final=ElfImage(Path(built).read_bytes())
    memory,starts,sizes,relocated=load_3dsx(binary)
    assert starts==[p[2] for p in final.loads]
    # Every loaded byte must match the finalized ELF, including native code,
    # native pointers, engine constants, and all three segment boundaries.
    for p in final.loads:
        offset=p[2]-starts[0];assert memory[offset:offset+p[4]]==final.data[p[1]:p[1]+p[4]],hex(p[2])
    words={v for name,(v,_,_) in final.symbols.items() if name.startswith('mp_be_fix_')}
    assert not words&relocated,'Loader must never reinterpret BE8 words as little endian'
    for address in words:
        expected=original.data[original.offset(address):original.offset(address)+4][::-1]
        assert memory[address-starts[0]:address-starts[0]+4]==expected,hex(address)
    layout=final.symbols['mp_expected_image_layout'][0]-starts[0]
    assert struct.unpack_from('<4I',memory,layout)==(0x4d50494d,*starts)
    # The native guard must reject an alternate loader placement before any
    # pre-resolved engine pointer can be used.
    moved,moved_starts,_,_=load_3dsx(binary,0x200000)
    assert list(struct.unpack_from('<4I',moved,layout)[1:])!=moved_starts
    return {'be8_words_verified':len(words),'native_loader_relocations':len(relocated),
            'loaded_segments_match_final_elf':True,'alternate_layout_rejected':True,
            'image_addresses':[hex(a) for a in starts]}

def reproduce_old_crash():
    directory=ROOT/'build/hardware/2026-09-10-startup'
    old=ElfImage((directory/'melee-crashed.elf').read_bytes())
    data,starts,sizes,_=load_3dsx(directory/'melee-crashed.3dsx')
    begin=old.symbols['mp_be_fixups_start'][0];end=old.symbols['mp_be_fixups_end'][0]
    for entry in range(begin,end,4):
        address=struct.unpack_from('<I',data,entry-starts[0])[0]
        segment=next(i for i,start in enumerate(starts) if start<=address<start+sizes[i])
        if not [5,4,6][segment]&2:
            assert address==0x371978,'First forbidden store must match the photographed FAR'
            return {'first_forbidden_write':hex(address),'photo_pc':'0x101634','segment_permissions':'RX'}
    raise AssertionError('Old startup unexpectedly avoided protected writes')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--release',action='store_true');args=ap.parse_args()
    directory=ROOT/('build/game-release' if args.release else 'build/game')
    binary=ROOT/('dist/hardware-startup-fix/3ds/melee/melee.3dsx' if args.release else 'dist/3ds/melee/melee-development.3dsx')
    result={'old_hardware_crash':reproduce_old_crash(),'fixed_image':check(directory/'melee-linked.elf',directory/'melee.elf',binary)}
    output=ROOT/('build/be8-image-release-test.json' if args.release else 'build/be8-image-smoke-test.json')
    output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
