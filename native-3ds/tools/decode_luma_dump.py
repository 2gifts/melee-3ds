"""Decode a Luma ARM11 dump and verify captured code against its matching ELF."""
import argparse,json,struct
from pathlib import Path
from be8_image import ElfImage

ap=argparse.ArgumentParser();ap.add_argument('dump',type=Path);ap.add_argument('elf',type=Path);args=ap.parse_args()
data=args.dump.read_bytes();header=struct.unpack_from('<IIHHHH6I',data)
magic0,magic1,minor,major,processor,core,kind,total,nregs,ncode,nstack,nextra=header
assert (magic0,magic1)==(0xdeadc0de,0xdeadcafe) and total==len(data)
assert 40+nregs+ncode+nstack+nextra==len(data) and nregs%4==0
registers=struct.unpack_from('<'+str(nregs//4)+'I',data,40)
names=[*('R'+str(i) for i in range(13)),'SP','LR','PC','CPSR','DFSR','IFSR','FAR','FPEXC','FPINST','FPINST2']
elf=ElfImage(args.elf.read_bytes())
symbols=sorted((v,n,s) for n,(v,s,section) in elf.symbols.items() if not n.startswith(('$','mp_be_fix_')) and s>0)
def symbol(a):
    hits=[(v,n,s) for v,n,s in symbols if v<=a<v+s]
    if not hits:return None
    v,n,s=hits[-1];return f'{n}+0x{a-v:x}'
pc=registers[15];code_start=pc+(2 if registers[16]&0x20 else 4)-ncode
code=data[40+nregs:40+nregs+ncode];off=elf.offset(code_start,ncode)
assert code==elf.data[off:off+ncode], 'Dump code does not match this ELF'
stack=data[40+nregs+ncode:40+nregs+ncode+nstack];candidates=[]
for offset in range(0,len(stack)-3,4):
    for order in ('big','little'):
        address=int.from_bytes(stack[offset:offset+4],order);label=symbol(address)
        if label and 0x100000<=address<0x500000 and address%4==0:candidates.append({'stack_offset':hex(offset),'byte_order':order,'address':hex(address),'symbol':label})
result={'dump_version':f'{major}.{minor}','processor':processor,'core':core,'exception_type':kind,
        'registers':{name:hex(value) for name,value in zip(names,registers)},'pc_symbol':symbol(pc),'lr_symbol':symbol(registers[14]),
        'captured_code_matches_elf':True,'captured_code_bytes':ncode,'stack_candidates_not_unwound_frames':candidates}
args.dump.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
