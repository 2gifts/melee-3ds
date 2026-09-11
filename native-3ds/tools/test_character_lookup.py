"""Regression for the original character ID retained by the PPC lookup."""
import subprocess
from build import ROOT, UPSTREAM, local_clang
from assets import dol_region, validate_dol
from engine_overlays import adapt
source=adapt(UPSTREAM/'src/melee/gm/gm_1601.c').read_text(encoding='utf-8')
start=source.index('f32 gm_80168B34(');end=source.index('\n}\n',start)+2
generated=ROOT/'build/generated';generated.mkdir(parents=True,exist_ok=True)
(generated/'character_lookup_function.inc').write_text(source[start:end])
start=source.index('float gm_80168BF8(');end=source.index('\n}\n',start)+2
(generated/'stock_lookup_wrapper.inc').write_text(source[start:end])
dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
# cmpwi r3,19; ble +8; addi r3,r3,-1; mulli r0,r5,30.
# The branch skips the only adjustment to r3 for ordinary characters.
assert dol_region(dol,0x80168bc0,16).hex()=='2c030013408100083863ffff1c05001e'
# bl 80168b34 followed by an integer-only epilogue: f1 is the result.
assert dol_region(dol,0x80168c3c,32).hex()=='4bfffef98001002483e1001c83c1001883a10014382100207c0803a64e800020'
binary=ROOT/'build/character-lookup-tests.exe'
subprocess.run([local_clang(),'-O2','-Werror=return-type','-I'+str(generated),str(ROOT/'tests/character_lookup_tests.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
