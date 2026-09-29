"""Folder layout of the game files on the SD card.

The 3DS scans a folder linearly each time it opens a file in it, so opening
one of the disc's 999 top-level files in a single flat folder took about
130 ms on hardware. The package spreads them over folders of about 30,
grouped by name so related files stay together:

    3ds/melee/files/_Ty03/TyMycR1A.dat
    3ds/melee/files/_Pl05/PlFx.dat
    3ds/melee/files/audio/...            (unchanged)

The game treats folders whose names start with '_' as transparent (see
port/3ds/file_io.c), and still reads the old flat layout.

Reorganize an existing card's files folder in place (moves only, no copies):

    python tools/sd_layout.py E:/3ds/melee/files
"""
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOLDER_FILES = 32
GROUP_MIN = 20


def folders(names):
    """Map every top-level disc file name to its folder name."""
    names = sorted(names, key=lambda n: (n.lower(), n))
    count = {}
    for name in names:
        count[name[:2]] = count.get(name[:2], 0) + 1
    groups = {}
    for name in names:
        prefix = name[:2]
        groups.setdefault(prefix if count[prefix] >= GROUP_MIN and prefix.isalnum() else 'Misc', []).append(name)
    result = {}
    for group, members in groups.items():
        chunks = math.ceil(len(members) / FOLDER_FILES)
        for i, name in enumerate(members):
            result[name] = f'_{group}{i * chunks // len(members) + 1:02d}' if chunks > 1 else f'_{group}'
    return result


def manifest_paths():
    manifest = json.loads((ROOT / 'assets/GALE01/manifest.json').read_text())
    return [entry['path'] for entry in manifest['files']]


def locations(paths):
    """Map each manifest path (disc-relative) to its path in the SD package."""
    top = folders(p for p in paths if '/' not in p)
    return {p: f'{top[p]}/{p}' if p in top else p for p in paths}


def organize(files_dir, dry_run=False):
    files_dir = Path(files_dir)
    if not files_dir.is_dir():
        raise SystemExit(f'{files_dir} is not a folder')
    moved = kept = 0
    for disc, location in locations(manifest_paths()).items():
        if disc == location:
            continue
        flat, target = files_dir / disc, files_dir / location
        if not flat.is_file():
            continue
        if target.is_file():
            if target.stat().st_size != flat.stat().st_size:
                raise SystemExit(f'{flat} and {target} differ; resolve this by hand')
            if not dry_run:
                flat.unlink()  # a duplicate from copying the new package over the old
            kept += 1
            continue
        if not dry_run:
            target.parent.mkdir(exist_ok=True)
            flat.rename(target)
        moved += 1
    print(f'{"Would move" if dry_run else "Moved"} {moved} files into folders'
          + (f'; {"would remove" if dry_run else "removed"} {kept} duplicate flat copies' if kept else ''))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--dry-run']
    if len(args) != 1:
        raise SystemExit(__doc__)
    organize(args[0], dry_run='--dry-run' in sys.argv)
