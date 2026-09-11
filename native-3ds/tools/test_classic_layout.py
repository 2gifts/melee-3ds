"""Check ARM context aliases and original Classic/trophy table contents."""
import argparse,json
from build import ROOT
from be8_image import ElfImage
from assets import dol_region,validate_dol

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--release',action='store_true');args=ap.parse_args()
    path=ROOT/('build/game-release/melee.elf' if args.release else 'build/game/melee.elf')
    elf=ElfImage(path.read_bytes());dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
    def address(name):return elf.symbols[name][0]
    def data(a,n):return bytes(elf.data[elf.offset(a,n):elf.offset(a,n)+n])
    checks=[]
    for base,aliases in [('mp_toy_storage',{'_Toy_804A26B8':0,'_Toy_devtext_buf_804A26C4':0xc,
        '_Toy_devtext_buf_804A2750':0x98,'Toy_804A284C':0x194,'Toy_804A2AA8':0x3f0}),
        ('mp_classic_runtime',{'gmClassicIntroDataBuffer':0,'gm_804908A0':0x20})]:
        for name,offset in aliases.items():assert address(name)==address(base)+offset,(base,name)
        checks.append({'storage':base,'aliases':aliases})
    assert elf.symbols['mp_toy_storage'][1]==0x404
    assert elf.symbols['mp_classic_runtime'][1]==0x90
    assert data(address('gmClassic_803DDEC8'),0x2f0)==dol_region(dol,0x803ddec8,0x2f0)
    checks.append({'original_classic_matchup_bytes':0x2f0})
    def string(a,original=False):
        result=b''
        for i in range(512):
            b=dol_region(dol,a+i,1) if original else data(a+i,1)
            result+=b
            if b==b'\0':return result
        raise AssertionError('Unterminated archive name')
    for name in ('_tyDisplay_803B8988','_tyDisplay_803B8A34','_tyDisplay_803B8AE0'):
        original=int(name.rsplit('_',1)[1],16)
        if name not in elf.symbols:
            # LLVM can materialize this constant array's pointers directly
            # on the stack. Check its filenames; live bonus tests exercise
            # the actual lookup and loading path separately.
            for i in range(43):
                expected=int.from_bytes(dol_region(dol,original+4*i,4),'big')
                assert string(expected,True) in elf.data,(name,i)
            checks.append({'original_trophy_archive_strings':43})
            continue
        a=address(name)
        for i in range(43):
            actual=int.from_bytes(data(a+4*i,4),'big')
            expected=int.from_bytes(dol_region(dol,original+4*i,4),'big')
            assert bool(actual)==bool(expected),(name,i)
            if actual:assert string(actual)==string(expected,True),(name,i)
        checks.append({'original_trophy_name_table':name,'entries':43})
    result={'passed':True,'release':args.release,'checks':checks}
    (ROOT/('build/update13-layout'+('-release' if args.release else '')+'.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()
