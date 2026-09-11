"""Check Venom's named tables against the original executable layout."""
import re, struct
from build import ROOT, UPSTREAM
from assets import dol_region, validate_dol
from engine_overlays import adapt

source = adapt(UPSTREAM/'src/melee/gr/grvenom.c').read_text()
dol = (ROOT/'assets/GALE01/sys/main.dol').read_bytes()
validate_dol(dol)
for address, count in [(0x803E5380,3), (0x803E5530,53),
                       (0x803E5644,10), (0x803E566C,5),
                       (0x803E5680,5), (0x803E56A0,6)]:
    match = re.search(r'grVe_'+f'{address:08X}'+r'\[\d+\] = \{([^}]+)\}',source)
    assert match, hex(address)
    values = [int(v.strip(),0) for v in match[1].split(',') if v.strip()]
    values += [0]*(count-len(values))
    assert struct.pack('>'+str(count)+'i',*values) == dol_region(dol,address,count*4)
assert dol_region(dol,0x803E5560,12*12) == bytes(144)
assert '&grVe_StageCallbacks[gobj_id]' in source
assert 'grVe_803E5644[gp->u.venom.xF4 * 2 + fire_kind]' in source
assert not re.search(r'\b(?:base|ptr|entry|new_var)\[',source.replace('`base[state + 0x7A]`',''))
print('Venom: six original tables, 12 spawn vectors and explicit callback/animation lookups verified')
