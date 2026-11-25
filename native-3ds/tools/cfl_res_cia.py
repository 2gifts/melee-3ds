"""Pack a console's Mii resource (CFL_Res.dat) as a CIA of its system data title, 0004009B00010202, so an Azahar profile
can install it. Without it, Azahar loads an open-source stand-in whose Mii heads have no real face parts.

    python3 tools/cfl_res_cia.py CFL_Res.dat OUT.cia [--makerom PATH]

Runs in the melee3ds-builder image (docker/cfl_res.sh calls it), which has makerom in the work volume. CFL_Res.dat is
Nintendo's file: dump it from your own console (for example with GodMode9) and keep it and the CIA out of the
repository (the launchers write the CIA to build/cfl-res/, which git ignores).

The title ID: category 0x009B is Contents (0x03) + CannotExecution (0x08) + System (0x10) + NotRequireRightForMount
(0x80), the last of which lets any application open it; unique ID 0x000102 and contents index 2 make 00010202."""
import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

RSF = '''BasicInfo:
  Title: CFL_Res
  ProductCode: CTR-N-CFLR
  ContentType: Application
TitleInfo:
  UniqueId: 0x000102
  ContentsIndex: 2
  CategoryFlags:
   - Contents
   - CannotExecution
   - System
   - NotRequireRightForMount
Option:
  FreeProductCode: true
  UseOnSD: false
  EnableCrypt: false
  EnableCompress: false
RomFs:
  RootPath: romfs
'''

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('dat', type=Path)
ap.add_argument('out', type=Path)
ap.add_argument('--makerom', default='/work/src/.toolchain/bin/makerom')
args = ap.parse_args()
size = args.dat.stat().st_size
if not 100_000 < size <= 1_083_400:   # cfl-mii accepts up to 1,083,400 bytes; the real file is about 1 MB
    raise SystemExit(f'{args.dat}: {size} bytes does not look like a console CFL_Res.dat')
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp) / 'romfs').mkdir()
    shutil.copyfile(args.dat, Path(tmp) / 'romfs/CFL_Res.dat')
    (Path(tmp) / 'cfl-res.rsf').write_text(RSF)
    subprocess.run([args.makerom, '-f', 'cia', '-target', 't', '-rsf', 'cfl-res.rsf', '-o', 'cfl-res.cia'],
                   cwd=tmp, check=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(Path(tmp) / 'cfl-res.cia', args.out)
print(f'{args.out}: title 0004009B00010202 from {size} bytes of CFL_Res.dat')
