"""Check relocated results storage and original camera tables against the DOL."""
import json, struct, subprocess
from pathlib import Path
from build import ROOT, local_clang
from assets import dol_region, validate_dol

elf=(ROOT/'build/game/melee.elf').read_bytes()
header=struct.unpack_from('<16sHHIIIIIHHHHHH',elf)
sections=[struct.unpack_from('<10I',elf,header[6]+40*i) for i in range(header[12])]
listing=subprocess.check_output([str(Path(local_clang()).with_name('llvm-nm.exe')),
                                str(ROOT/'build/game/melee.elf')],text=True)
symbols={p[2]:int(p[0],16) for line in listing.splitlines() if len(p:=line.split())==3}
def region(address,size):
    for s in sections:
        if s[1]!=8 and s[3]<=address and address+size<=s[3]+s[5]:
            offset=s[4]+address-s[3];return elf[offset:offset+size]
    raise ValueError('Missing ELF data range')

base=symbols['mp_results_display']
for name,offset in [('lbl_8046E1B0',0),('lbl_8046E38C',0x1dc),('lbl_8046E39C',0x1ec),('lbl_8046E3AC',0x1fc)]:
    assert symbols[name]==base+offset,(name,hex(symbols[name]),hex(base+offset))

dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
checks=[]
for name,address,size in [('ftMapping_list',0x803bcde0,0x63),
                          ('gmResultCharacterScaleData',0x803d6a18,0x600),
                          ('gmResultCharacterData',0x803d7058,0x890),
                          ('gmResultScoreTableInit',0x803d7038,0x20),
                          ('gmResultX22F4Init',0x803d7018,0x20),
                          ('gmResultCameraDesc',0x803d7910,0x38)]:
    actual=region(symbols[name],size);expected=dol_region(dol,address,size)
    if name=='gmResultCameraDesc':
        # Only the two world-object pointers move when linked to ARM.
        actual=actual[:24]+actual[32:];expected=expected[:24]+expected[32:]
    assert actual==expected,name+' differs from the supported original DOL'
    checks.append({'table':name,'verified_bytes':len(actual)})
result={'shared_display_storage':True,'original_tables':checks}
(ROOT/'build/results-layout-test.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
