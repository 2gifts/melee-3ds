"""The top-screen Solo / Group submenu's art (port/engine/group/menu_art.c) over the disc's own menu archives, on the PC.

menu_art.c is compiled with tools/group/src/menu_art_unit.c and given the tables that menu_art_hook.c reads in the
game (MenMainCursor_Top joint 3's row labels, MenMainPanel_Top joint 85's submenu tabs, and their image tracks):

  * vanilla's MnMaAll.usd and Slippi's: accepted, the same three images built from both, frame 2400's tab key (and no
    other) moves from tab 6 (Tournament Melee) to tab 13, and frames 220 / 222 show labels 49 / 50;
  * the Japanese MnMaAll.dat: refused (its labels are not the English letters the art is cut from);
  * modified archives: refused (a filled label slot, a filled tab slot, a source image with one byte changed, a tab track
    whose frame 2400 shows another tab, a short table);
  * the images: each word centred, letters white inside a dark outline, nothing past the outline.

Previews (the three images over the menu's blue) go to $MELEE_TEST_OUT/menu-art/.

    python tools/group/menu_art_test.py [--files DIR] [--slippi-files DIR]
    docker/emu-test.sh run tools/group/menu_art_test.py      # in the test container, over the work volume's files
DIR: the disc's files folder (holding _Misc02/MnMaAll.usd) and Slippi's files folder (holding MnMaAll.usd). By default
the work volume's (/work/src/dist/native-alpha/3ds/melee/...) and $MELEE_GAME_FILES / $MELEE_SLIPPI_FILES are tried."""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from hsd_model import Dat  # noqa: E402

LABEL_JOINT, TAB_JOINT = 3, 85
SOLO, GROUP, TAB = 49, 50, 13
failures = []


def check(name, ok, detail=''):
    print(f'{"PASS" if ok else "FAIL"} {name}{(": " + detail) if detail and not ok else ""}')
    if not ok:
        failures.append(name)


def mat_joints(d, root):
    out = []

    def walk(o):
        while o:
            out.append(o)
            walk(d.u32(o))
            o = d.u32(o + 4)
    walk(root)
    return out


def size_of(w, h, fmt):
    return (w + 7) // 8 * ((h + 3) // 4) * 32 if fmt == 2 else (w + 7) // 8 * ((h + 7) // 8) * 32


class Tables:
    """A joint's first texture animation: its images (w, h, fmt, bytes) and its image track (frac, bytes)."""

    def __init__(self, d, model, joint):
        mj = mat_joints(d, d.roots[model + '_matanim_joint'])[joint]
        ta = d.u32(d.u32(mj + 8) + 8)
        table, n = d.u32(ta + 12), d.u16(ta + 20)
        self.images = []
        for i in range(n):
            im = d.u32(table + 4 * i)
            ptr, w, h, fmt = d.u32(im), d.u16(im + 4), d.u16(im + 6), d.u32(im + 8)
            self.images.append([w, h, fmt, bytes(d.b[ptr:ptr + size_of(w, h, fmt)])])
        fo = d.u32(d.u32(ta + 8) + 8)
        length, frac, ad = d.u32(fo + 4), d.u8(fo + 13), d.u32(fo + 16)
        self.frac, self.track = frac, bytearray(d.b[ad:ad + length])


def load(path):
    d = Dat.load(path)
    return Tables(d, 'MenMainCursor_Top', LABEL_JOINT), Tables(d, 'MenMainPanel_Top', TAB_JOINT)


def blob(labels, tabs):
    out = struct.pack('<II', len(labels.images), len(tabs.images))
    for w, h, fmt, px in labels.images + tabs.images:
        out += struct.pack('<IIII', w, h, fmt, len(px)) + px
    for t in (labels, tabs):
        out += struct.pack('<II', t.frac, len(t.track)) + bytes(t.track)
    return out


def build_unit(tmp):
    exe = Path(tmp) / 'menu_art_unit'
    cc = os.environ.get('CC', 'cc')
    subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-I', str(ROOT / 'port/engine/group'),
                    str(HERE / 'src/menu_art_unit.c'), str(ROOT / 'port/engine/group/menu_art.c'), '-o', str(exe)],
                   check=True)
    return exe


def run(exe, labels, tabs, tmp, tag):
    folder = Path(tmp) / tag
    folder.mkdir(exist_ok=True)
    (folder / 'tables.bin').write_bytes(blob(labels, tabs))
    out = subprocess.run([str(exe), str(folder / 'tables.bin'), str(folder)], check=True, capture_output=True,
                         text=True).stdout
    images = {n: (folder / f'{n}.bin').read_bytes() for n in ('solo', 'group', 'tab') if (folder / f'{n}.bin').exists()}
    return out, images


def ia4_pixels(b, w=176, h=30):
    return [[b[((y // 4) * (w // 8) + x // 8) * 32 + (y % 4) * 8 + x % 8] for x in range(w)] for y in range(h)]


def i4_pixels(b, w=168, h=28):
    def at(x, y):
        v = b[((y // 8) * (w // 8) + x // 8) * 32 + (y % 8) * 4 + (x % 8) // 2]
        return v & 15 if x % 2 else v >> 4
    return [[at(x, y) for x in range(w)] for y in range(h)]


def png(path, rows):
    w, h = len(rows[0]), len(rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\x00' + bytes(c for px in r for c in px) for r in rows)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def over_blue(alpha, intensity):
    a, i = alpha / 15, intensity * 17
    return tuple(round(i * a + bg * (1 - a)) for bg in (40, 60, 160))


def preview(path, images):
    rows = []
    for name in ('solo', 'group'):
        px = ia4_pixels(images[name])
        rows += [[over_blue(v >> 4, v & 15) for v in r] + [(40, 60, 160)] * 0 for r in px] + [[(0, 0, 0)] * 176]
    tab = i4_pixels(images['tab'])
    rows += [[over_blue(v, 15) for v in r] + [(40, 60, 160)] * 8 for r in tab]
    big = [[px for px in r for _ in range(3)] for r in rows for _ in range(3)]
    png(path, big)


def word_checks(name, b):
    px = ia4_pixels(b)
    cols = [x for x in range(176) if any(px[y][x] >> 4 for y in range(30))]
    left, right = cols[0], 175 - cols[-1]
    check(f'{name}: centred (margins {left} / {right})', abs(left - right) <= 1)
    fill = sum(1 for r in px for v in r if v >> 4 and (v & 15) >= 8)
    edge = sum(1 for r in px for v in r if v >> 4 and (v & 15) < 8)
    check(f'{name}: white letters ({fill} px) in a dark outline ({edge} px)', fill > 300 and edge > 300)
    # every white pixel has outline or white around it, never the blue (a letter cut without its outline)
    bare = 0
    for y in range(30):
        for x in range(176):
            if px[y][x] >> 4 == 15 and (px[y][x] & 15) >= 12:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < 30 and 0 <= xx < 176 and px[yy][xx] >> 4 == 0:
                            bare += 1
    check(f'{name}: no white pixel touches the background ({bare})', bare == 0)
    return cols[-1] - cols[0] + 1


def find(paths):
    for p in paths:
        if p and Path(p).is_file():
            return Path(p)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--files', help="the disc's files folder (with _Misc02/MnMaAll.usd)")
    ap.add_argument('--slippi-files', help="Slippi's files folder (with MnMaAll.usd)")
    args = ap.parse_args()
    work = '/work/src/dist/native-alpha/3ds/melee'
    files = [args.files, os.environ.get('MELEE_GAME_FILES'), work + '/files']
    slippi = [args.slippi_files, os.environ.get('MELEE_SLIPPI_FILES'), work + '/slippi/files']
    usd = find([f and f + '/_Misc02/MnMaAll.usd' for f in files])
    dat = find([f and f + '/_Misc02/MnMaAll.dat' for f in files])
    slp = find([f and f + '/MnMaAll.usd' for f in slippi])
    if usd is None:
        raise SystemExit('MnMaAll.usd not found: pass --files (the disc\'s files folder)')
    out_dir = Path(os.environ.get('MELEE_TEST_OUT') or ROOT / 'build/emu-test') / 'menu-art'
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        exe = build_unit(tmp)
        labels, tabs = load(usd)
        out, vanilla = run(exe, labels, tabs, tmp, 'vanilla')
        print(out.strip())
        check('vanilla MnMaAll.usd is accepted', out.startswith('check ok'), out)
        check('frame 2400 shows tab 6, then tab 13', 'tab 2400 before 6 after 13' in out, out)
        changed = [line for line in out.splitlines() if line.startswith('tab changed at')]
        check('no other tab key changes', changed == ['tab changed at 2400: 6 -> 13'], out)
        check('frames 220 / 222 / 224 / 226 show labels 49 to 52', 'label 220 49 222 50 224 51 226 52' in out, out)
        if vanilla:
            preview(out_dir / 'menu-art.png', vanilla)
            print(f'preview: {out_dir / "menu-art.png"}')
            solo_w = word_checks('Solo', vanilla['solo'])
            group_w = word_checks('Group', vanilla['group'])
            check(f'Group ({group_w} px) is wider than Solo ({solo_w} px)', group_w > solo_w)
            tab = i4_pixels(vanilla['tab'])
            cols = [x for x in range(168) if any(tab[y][x] for y in range(28))]
            check(f'the tab is centred ({cols[0]} / {167 - cols[-1]})', abs(cols[0] - (167 - cols[-1])) <= 2)

        if slp:
            out, slippi_images = run(exe, *load(slp), tmp, 'slippi')
            check("Slippi's MnMaAll.usd is accepted", out.startswith('check ok'), out)
            check("Slippi's archive gives the same images", slippi_images == vanilla)
            check("Slippi's labels 33 / 34 (Ranked, Unranked) are not touched",
                  'label 220 49 222 50' in out and load(slp)[0].images[33][0] == 176, out)
        else:
            print('SKIP Slippi: its MnMaAll.usd was not found (--slippi-files)')
        if dat:
            out, _ = run(exe, *load(dat), tmp, 'japanese')
            check('the Japanese MnMaAll.dat is refused', out.startswith('check refused a source image'), out)
        else:
            print('SKIP Japanese: MnMaAll.dat not found')

        def modified(tag, change, expect):
            lab, tb = load(usd)
            change(lab, tb)
            out, _ = run(exe, lab, tb, tmp, tag)
            check(f'refused: {tag}', out.startswith('check refused ' + expect), out)

        modified('label slot 49 holds an image', lambda lab, tb: lab.images.__setitem__(SOLO, list(lab.images[10])),
                 'label slots')
        modified('label slot 52 holds an image', lambda lab, tb: lab.images.__setitem__(52, list(lab.images[10])),
                 'label slots')
        modified('tab slot 13 holds an image', lambda lab, tb: tb.images.__setitem__(TAB, list(tb.images[7])),
                 'tab slot')

        def flip(lab, tb):
            px = bytearray(lab.images[20][3])
            px[1000] ^= 0x11
            lab.images[20][3] = bytes(px)
        modified('a source letter changed', flip, 'a source image')

        def tab_key(lab, tb):
            k = tb.track.index(6 << 3)   # frame 2400's key (tab 6, u8 with shift 3): the only 48 in the track
            tb.track[k] = 7 << 3
        modified('the tab track shows tab 7 at frame 2400', tab_key, 'the tab track')

        def short(lab, tb):
            del lab.images[52:]
        modified('a short label table', short, 'unexpected table sizes')

    print(f'{len(failures)} failure(s)' if failures else 'menu art: all checks passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
