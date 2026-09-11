"""Preserve the original PPC boolean return on the zero-damage path."""
import subprocess
from build import ROOT,UPSTREAM,local_clang
from engine_overlays import adapt
from assets import dol_region,validate_dol
dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
code=dol_region(dol,0x8008da4c,0xc4)
assert code[:4].hex()=='7c0802a6' # mflr r0
assert code[0x1c:0x20].hex()=='41820094' # zero-damage beq to the epilogue
assert code[0xb0:0xb4].hex()=='7c030378' # mr r3,r0 (nonzero caller address)
source=adapt(UPSTREAM/'src/melee/ft/kinds/ftCommon/ftCo_Damage.c').read_text()
start=source.index('bool ftCo_8008DA4C(Fighter_GObj* gobj, HitElement arg1, enum_t arg2)\n{')
end=source.index('\n}\n',start)+2
generated=ROOT/'build/generated';generated.mkdir(parents=True,exist_ok=True)
(generated/'damage_return_function.inc').write_text(source[start:end])
exe=ROOT/'build/damage-return-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-Wextra','-I'+str(generated),str(ROOT/'tests/damage_return_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
