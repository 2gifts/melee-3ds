"""Derive the stage select cursor's GX program with the decomp's TEV compiler.

Compiles sysdolphin's texp.c and texpdag.c on the host with tests/cursor_texp.c,
which rebuilds the cursor material's expression and records the compiled stages
in port/engine/gx.c's layout. Writes tests/fixtures/cursor-material.json, the
program tests/cursor_material_tests.c checks the matcher against.

The decomp's dolphin/types.h makes u32 an unsigned long (64 bits on a 64-bit
host); the TEV scheduler stores dependency masks and indices through u32 and
int pointers interchangeably, so it is compiled against a copy with 32-bit
u32/s32 (and the one header that spells the same type out as unsigned long).

    python3 tools/derive_cursor_material.py [--check]

--check compares with the committed fixture instead of writing it."""
import json,subprocess,sys
from build import ROOT,UPSTREAM,local_clang

FIXTURE=ROOT/'tests/fixtures/cursor-material.json'


def main():
    base=UPSTREAM if (UPSTREAM/'src/sysdolphin/baselib/texp.c').exists() else ROOT.parent
    include=base/'extern/dolphin/include/dolphin'
    shim=ROOT/'build/generated/cursor-texp';(shim/'dolphin/os').mkdir(parents=True,exist_ok=True)
    types=(include/'types.h').read_text()
    types=types.replace('typedef unsigned long u32;','typedef unsigned int u32;').replace('typedef signed long s32;','typedef signed int s32;')
    (shim/'dolphin/types.h').write_text(types)
    (shim/'dolphin/os/OSRtc.h').write_text((include/'os/OSRtc.h').read_text().replace('unsigned long','unsigned int'))
    exe=ROOT/'build/cursor-texp.exe'
    subprocess.run([local_clang(),'-std=gnu99','-O0','-w','-I'+str(shim),'-I'+str(base/'src'),'-I'+str(base/'src/MSL'),'-I'+str(base/'extern/dolphin/include'),
        str(ROOT/'tests/cursor_texp.c'),str(base/'src/sysdolphin/baselib/texp.c'),str(base/'src/sysdolphin/baselib/texpdag.c'),
        '-o',str(exe)],check=True)
    derived=json.loads(subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout)
    if '--check' in sys.argv:
        committed=json.loads(FIXTURE.read_text())
        assert committed==derived,'tests/fixtures/cursor-material.json differs from the derived program'
        print('Cursor material fixture matches the decomp TEV compiler')
    else:
        FIXTURE.write_text(json.dumps(derived,indent=2)+'\n')
        print('Wrote',FIXTURE.relative_to(ROOT))
    c=derived['tev_configuration']
    for i in range(derived['stages']):
        s=c[i*30:i*30+25]
        print(f'stage {i}: order {s[0:3]} color {s[3:7]} op {s[11:16]} alpha {s[7:11]} op {s[16:21]} swap {s[21:23]} ksel {s[23:25]}')


if __name__=='__main__':main()
