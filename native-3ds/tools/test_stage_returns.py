"""Verify explicit ARM results and original PPC return-register evidence."""
import subprocess,sys
from build import ROOT, UPSTREAM, local_clang
from engine_overlays import adapt
from assets import dol_region,validate_dol
sys.path.insert(0,str(ROOT/'.toolchain/disassembly-python'))
from capstone import Cs,CS_ARCH_PPC,CS_MODE_32,CS_MODE_BIG_ENDIAN
dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
code=dol_region(dol,0x801f96e0,0x1c8)
instructions=list(Cs(CS_ARCH_PPC,CS_MODE_32|CS_MODE_BIG_ENDIAN).disasm(code,0x801f96e0))
assert instructions[-1].mnemonic=='blr'
assert not any(i.op_str.startswith('f1,') for i in instructions)
assert dol_region(dol,0x802efe14,20).hex()=='4bf853c58001000c382100087c0803a64e800020'
parts=[]
for name,signature in [('gricemt.c','float grIceMt_801F96E0('),
                       ('itkyasarinegg.c','bool itKyasarinegg_UnkMotion4_Anim(')]:
    path=next(UPSTREAM.rglob(name));source=adapt(path).read_text(encoding='utf-8')
    start=source.index(signature);end=source.index('\n}',start)+2
    parts.append(source[start:end])
generated=ROOT/'build/generated/stage_return_functions.inc';generated.write_text('\n'.join(parts))
exe=ROOT/'build/stage-return-tests.exe'
subprocess.run([local_clang(),'-O2','-Werror=return-type','-I'+str(generated.parent),
    str(ROOT/'tests/stage_return_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
