"""Make Slippi's menu files from your own disc files and put them on the SD card.

Slippi Dolphin patches a few of Melee's menu archives when it runs:
- the "Online Play" entry and the Online submenu labels (MnMaAll);
- their descriptions (SdMenu);
- the stage select screen (MnSlMap);
- character-select text (SdSlChr);
- name entry (MnExtAll).
The patches (tools/slippi/gamefiles/*.usd.diff) are VCDIFF deltas copied from
project-slippi/dolphin, Data/Sys/GameFiles/GALE01 (commit 41a7a3a, GPL-2.0+).
This applies them to the .usd files found under FILES (the disc files the
port already uses, flat or in folders) and writes the results to OUT. The
3DS port loads anything in sdmc:/3ds/melee/slippi/files/ in place of the
disc file of the same name (port/3ds/file_io.c).

    python tools/slippi/slippi_files.py E:/3ds/melee/files E:/3ds/melee/slippi/files
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from vcdiff import decode  # noqa: E402

NAMES = ('MnMaAll.usd', 'SdMenu.usd', 'MnSlMap.usd', 'SdSlChr.usd', 'MnExtAll.usd')


def find(root, name):
    direct = root / name
    if direct.exists():
        return direct
    for path in root.rglob(name):
        return path
    return None


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    files, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    made = 0
    for name in NAMES:
        source = find(files, name)
        if source is None:
            print(f'{name}: not found under {files}; skipped')
            continue
        try:
            data = decode(source.read_bytes(), (HERE / 'gamefiles' / (name + '.diff')).read_bytes())
        except ValueError as e:
            print(f'{name}: {e}; skipped (is this a NTSC 1.02 disc?)')
            continue
        if len(data) < 4 or int.from_bytes(data[:4], 'big') != len(data):
            print(f'{name}: result is not a valid archive; skipped')
            continue
        (out / name).write_bytes(data)
        made += 1
        print(f'{name}: {len(data)} bytes')
    print(f'{made} of {len(NAMES)} files written to {out}')


if __name__ == '__main__':
    main()
