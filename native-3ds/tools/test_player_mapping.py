"""Exercise adapted player consumers against the supported DOL's mapping bytes."""
import subprocess
from build import ROOT, UPSTREAM, local_clang
from assets import dol_region, validate_dol
from engine_overlays import adapt

source=adapt(UPSTREAM/'src/melee/pl/player.c').read_text()
generated=ROOT/'build/generated';generated.mkdir(parents=True,exist_ok=True)
functions=[]
for signature in ('void Player_80032070(', 'Gm_PKind Player_8003248C(',
                  's8 Player_80032610(', 's32 Player_GetFalls(',
                  'void Player_80036E20('):
    start=source.index(signature);end=source.index('\n}\n',start)+2
    functions.append(source[start:end])
assert not any('vec_arr' in function for function in functions)
(generated/'player_mapping_functions.inc').write_text('\n'.join(functions))
dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
mapping=dol_region(dol,0x803bcde0,0x63)
(generated/'player_mapping_reference.inc').write_text(
    'static const unsigned char original_mapping[]={'+','.join(map(str,mapping))+'};\n')
binary=ROOT/'build/player-mapping-tests.exe'
subprocess.run([local_clang(),'-O2','-I'+str(generated),str(ROOT/'tests/player_mapping_tests.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
