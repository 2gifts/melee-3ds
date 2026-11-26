"""Dump every stock icon of IfAll.usd as a contact sheet, each with its table index, to build the bottom screen's
fighter x costume -> icon table (bottom_draw.c stock_icon).

IfAll.usd (HSD archive; data at +32) has an image-descriptor table at +774116 and a palette table at +774636 (pointers,
big-endian), one entry per stock icon: 24 x 24 CI4 images with 16-entry RGB5A3 palettes (the offsets bottom_draw.c's
loader uses). Sheik's icons are entries 25, 51, 77, 103 and 114, which checks the reading.

    python tools/dump_stock_icons.py [--usd assets/GALE01/files/IfAll.usd] [--out build/stock-icons.png]
Plain Python (zlib); no image library needed.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DESC, PAL = 774116, 774636
COUNT = (PAL - DESC) // 4
DIGITS = {  # 3 x 5 pixel digits for the labels
    '0': '111101101101111', '1': '010110010010111', '2': '111001111100111', '3': '111001111001111',
    '4': '101101111001001', '5': '111100111001111', '6': '111100111101111', '7': '111001001001001',
    '8': '111101111101111', '9': '111101111001111'}


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def decode(data, index):
    """The icon's 24 x 24 RGBA pixels (rows of (r, g, b, a)), or None if the entry does not look like a stock icon."""
    b = data[32:]
    n = be32(data, 4)
    d, p = be32(b, DESC + index * 4), be32(b, PAL + index * 4)
    if d + 24 > n or p + 16 > n:
        return None
    im, pal = be32(b, d), be32(b, p)
    if be16(b, d + 4) != 24 or be16(b, d + 6) != 24 or be32(b, d + 8) != 8 or be32(b, p + 4) != 2 or \
            be16(b, p + 12) != 16 or im + 288 > n or pal + 32 > n:
        return None
    rows = []
    for y in range(24):
        row = []
        for x in range(24):
            t = ((y // 8) * 3 + x // 8) * 64 + (y % 8) * 8 + x % 8
            v = be16(b, pal + 2 * ((b[im + t // 2] >> (0 if t & 1 else 4)) & 15))
            if v & 0x8000:
                row.append((((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255))
            else:
                row.append((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7))
        rows.append(row)
    return rows


def write_png(path, w, h, pixels):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(c for px in row for c in px) for row in pixels)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--usd', default=str(ROOT / 'assets/GALE01/files/IfAll.usd'))
    ap.add_argument('--out', default=str(ROOT / 'build/stock-icons.png'))
    ap.add_argument('--first', type=int, default=0, help='the first entry to show')
    ap.add_argument('--scale', type=int, default=1, help='draw each pixel this many times')
    args = ap.parse_args()
    data = Path(args.usd).read_bytes()
    cols, cell = 13, 40
    shown = range(args.first, COUNT)
    rows_n = (len(shown) + cols - 1) // cols
    w, h = cols * cell, rows_n * cell
    img = [[(40, 48, 60)] * w for _ in range(h)]
    good = 0
    for slot, i in enumerate(shown):
        x0, y0 = (slot % cols) * cell, (slot // cols) * cell
        icon = decode(data, i)
        if icon:
            good += 1
            for y in range(24):
                for x in range(24):
                    r, g, bl, a = icon[y][x]
                    bg = img[y0 + 4 + y][x0 + 8 + x]
                    img[y0 + 4 + y][x0 + 8 + x] = tuple((c * a + o * (255 - a)) // 255 for c, o in zip((r, g, bl), bg))
        for k, ch in enumerate(str(i)):
            for py in range(5):
                for px in range(3):
                    if DIGITS[ch][py * 3 + px] == '1':
                        img[y0 + 31 + py][x0 + 12 + k * 4 + px] = (255, 255, 255)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    if args.scale > 1:
        img = [[px for px in row for _ in range(args.scale)] for row in img for _ in range(args.scale)]
        w, h = w * args.scale, h * args.scale
    write_png(args.out, w, h, img)
    print(f'{good} of {COUNT} entries decode as stock icons -> {args.out}')
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
