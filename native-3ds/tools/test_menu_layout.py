"""Verify linked menu tables and SJIS name bytes against the owned US1.02 DOL."""
import json,struct,argparse
from build import ROOT
from assets import dol_region,validate_dol
from be8_image import ElfImage

ap=argparse.ArgumentParser();ap.add_argument('--release',action='store_true');args=ap.parse_args()
elf=ElfImage((ROOT/('build/game-release/melee.elf' if args.release else 'build/game/melee.elf')).read_bytes())
dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
def region(a,n):
    for p in elf.loads:
        if p[2]+p[4]<=a and a+n<=p[2]+p[5]:return b'\0'*n
    return bytes(elf.data[elf.offset(a,n):elf.offset(a,n)+n])
def word(a):return int.from_bytes(region(a,4),'big')
def cstring(a,original=False):
    data=bytearray()
    for i in range(512):
        b=dol_region(dol,a+i,1) if original else region(a+i,1)
        data+=b
        if b==b'\0':return bytes(data)
    raise ValueError('Unterminated name')
checks=[]
tables=[('mnItemSw_803ED340',0x803ed340,48),('mnItemSw_AnimTable',0x803ed370,200),('mnItemSw_803ED438',0x803ed438,32),
        ('mnName_803ED538',0x803ed538,48),('mnName_803ED600',0x803ed600,24),('mnName_803ED618',0x803ed618,24)]
tables += [(name,int(name.split('_')[-1],16),132) for name in ('lbl_803B75F8','lbl_803B767C','lbl_803B7700','lbl_803B7784')]
tables += [('mnNameNew_803EDA58',0x803eda58,36),('unk_vec',0x803ee324,12),('mnNameNew_803EE330',0x803ee330,12),
           ('HSD_SisLib_FontAtlas',0x8040cd40,287*512),('HSD_SisLib_8040C680',0x8040c680,576),
           ('lbl_8040C8C0',0x8040c8c0,576),('HSD_SisLib_8040CB00',0x8040cb00,576)]
for name,address,n in tables:
    assert region(elf.symbols[name][0],n)==dol_region(dol,address,n),name
    checks.append({'table':name,'original_bytes_verified':n})
for name,n in [('lbl_803D4D74',33),('lbl_803D4FDC',33),('lbl_803D5060',33),('lbl_803D50E4',33)]:
    address=int(name.split('_')[-1],16);actual=elf.symbols[name][0];count=0
    for i in range(n):
        dp=int.from_bytes(dol_region(dol,address+i*4,4),'big');ep=word(actual+i*4)
        if not dp:assert ep==0
        else:assert cstring(ep)==cstring(dp,True),(name,i,cstring(ep),cstring(dp,True));count+=1
    checks.append({'table':name,'original_strings_verified':count})
for name,address,offset,n in [('mnNameNew_KeyMap',0x803eda7c,16,150),('mnNameNew_GlyphTable',0x803edce4,0,400)]:
    actual=elf.symbols[name][0];count=0
    if offset:assert region(actual,offset)==dol_region(dol,address,offset)
    for i in range(n):
        dp=int.from_bytes(dol_region(dol,address+offset+i*4,4),'big');ep=word(actual+offset+i*4)
        if not dp:assert ep==0,(name,i)
        else:assert cstring(ep)==cstring(dp,True),(name,i);count+=1
    checks.append({'table':name,'original_strings_verified':count})
for i,name in enumerate(('mnName_803ED568','mnName_803ED574','mnName_803ED580','mnName_803ED58C','mnName_803ED598','mnName_803ED5A4')):
    actual=word(elf.symbols['mnName_803B8510'][0]+i*4)
    assert actual==elf.symbols[name][0],name
    assert region(actual,12)==dol_region(dol,int(name.split('_')[-1],16),12),name
result={'image':'release' if args.release else 'smoke','tables':checks,'name_loop_pointers':6}
(ROOT/('build/match-fix-menu-layout'+('-release' if args.release else '')+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
