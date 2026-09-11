"""Build the portable bottom-screen compositor tests using locally owned art."""
import json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from assets import ROOT,dol_region,validate_dol
from build import local_clang

def main():
    dol=(ROOT/'assets/GALE01/sys/main.dol').read_bytes();validate_dol(dol)
    (ROOT/'build/bottom-font.bin').write_bytes(dol_region(dol,0x8040cd40,287*512))
    exe=ROOT/'build/bottom-screen-test.exe'
    subprocess.run([str(local_clang()),'-Wall','-Wextra','-Werror','-O2',
        str(ROOT/'tests/bottom_screen.c'),str(ROOT/'port/3ds/bottom_draw.c'),'-o',str(exe)],cwd=ROOT,check=True)
    subprocess.run([str(exe)],cwd=ROOT,check=True)
    files=[]
    for p in sorted((ROOT/'build').glob('bottom-host-*.rgb565')):
        a=np.rot90(np.fromfile(p,dtype='<u2').reshape(320,240)).astype(np.uint32)
        rgb=np.array([(a>>11)*255//31,((a>>5)&63)*255//63,(a&31)*255//31],dtype=np.uint8).transpose(1,2,0)
        dest=p.with_suffix('.png');Image.fromarray(rgb).save(dest);files.append(dest.name)
    (ROOT/'build/bottom-host-test.json').write_text(json.dumps({'passed':True,'images':files,'source':'original locally extracted US 1.02 art'},indent=2))
    print('Original-art compositor fixtures rendered:',len(files))

if __name__=='__main__':main()
