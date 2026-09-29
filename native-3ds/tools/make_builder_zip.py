"""Make the one-click builder download for a GitHub release.

  python tools/make_builder_zip.py 1.1.0 [--allow-dirty]

The zip holds only this repository's tracked native-3ds source; the user's
disc supplies the game. Layout:

  Melee 3DS CIA Builder/Build Melee CIA.bat   (tools/easy_build/)
  Melee 3DS CIA Builder/READ ME FIRST.txt
  Melee 3DS CIA Builder/builder/...            (native-3ds, BUILDER_VERSION)
"""
import argparse
import subprocess
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOP = 'Melee 3DS CIA Builder'
# Never ship game data or built executables, whatever is tracked.
FORBIDDEN = {'.iso', '.gcm', '.rvz', '.dol', '.dat', '.usd', '.hps', '.ssm', '.thp', '.3dsx', '.cia', '.elf',
             '.cgfx', '.bnr', '.wav'}
WINDOWS_TEXT = {'.bat', '.ps1', '.txt'}


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('version')
    ap.add_argument('--allow-dirty', action='store_true', help='Include uncommitted changes (testing only)')
    args = ap.parse_args()
    if not args.allow_dirty and git('status', '--porcelain', '--', '.').strip():
        raise SystemExit('native-3ds has uncommitted changes; commit them or pass --allow-dirty')
    names = git('ls-files', '-z', '--', '.').split('\0')
    if args.allow_dirty:
        names += git('ls-files', '-z', '--others', '--exclude-standard', '--', '.').split('\0')
    names = sorted({n for n in names if n and (ROOT/n).is_file()})
    bad = [n for n in names if Path(n).suffix.lower() in FORBIDDEN]
    if bad:
        raise SystemExit(f'Refusing to package game or build files: {bad[:5]}')
    stamp = time.localtime(int(git('log', '-1', '--format=%ct').strip()))[:6]
    out = ROOT/f'dist/Melee-3DS-CIA-Builder-v{args.version}.zip'
    out.parent.mkdir(parents=True, exist_ok=True)

    def add(z, arcname, data):
        info = zipfile.ZipInfo(f'{TOP}/{arcname}', stamp)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        if Path(arcname).suffix.lower() in WINDOWS_TEXT:
            data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        z.writestr(info, data)

    with zipfile.ZipFile(out, 'w') as z:
        easy = ROOT/'tools/easy_build'
        add(z, 'Build Melee CIA.bat', (easy/'Build Melee CIA.bat').read_bytes())
        add(z, 'READ ME FIRST.txt', (easy/'READ ME FIRST.txt').read_bytes())
        for name in names:
            add(z, f'builder/{name}', (ROOT/name).read_bytes())
        add(z, 'builder/BUILDER_VERSION', f'{args.version}\n'.encode())
    print(f'{out} ({out.stat().st_size/1e6:.1f} MB, {len(names)} source files)')


if __name__ == '__main__':
    main()
