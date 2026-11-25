"""Build and run the portable bottom-screen compositor tests (tests/bottom_screen.c) with locally owned art, and turn
each image into a PNG next to it (build/bottom-host-*.png).

    python tools/test_bottom_screen.py
Needs the extracted disc (assets/GALE01: main.dol's font, MnSlChr.usd, IfAll.usd); in Docker, run it in the builder
image with the work volume (docker/README.md). The group pages are drawn from fixed data (tests/bottom_screen_stubs.c).
"""
import json
import os
import struct
import subprocess
import zlib
from pathlib import Path

from assets import ROOT, dol_region, validate_dol


def png(path, w, h, rows):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    dol = (ROOT / 'assets/GALE01/sys/main.dol').read_bytes()
    validate_dol(dol)
    (ROOT / 'build').mkdir(exist_ok=True)
    (ROOT / 'build/bottom-font.bin').write_bytes(dol_region(dol, 0x8040cd40, 287 * 512))
    exe = ROOT / 'build/bottom-screen-test.exe'
    cc = os.environ.get('CC') or 'cc'
    inc = ['-I' + str(ROOT / d) for d in ('port/include', 'port/3ds', 'port/3ds/localwireless', 'port/3ds/group',
                                         'port/3ds/slippi')]
    subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-O2', *inc, str(ROOT / 'tests/bottom_screen.c'),
                    str(ROOT / 'tests/bottom_screen_stubs.c'), str(ROOT / 'port/3ds/bottom_draw.c'),
                    str(ROOT / 'port/3ds/name_text.c'), '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], cwd=ROOT, check=True)
    files = []
    for p in sorted((ROOT / 'build').glob('bottom-host-*.rgb565')):
        d = p.read_bytes()   # 320 columns of 240 pixels, bottom to top
        rows = []
        for y in range(240):
            r = bytearray()
            for x in range(320):
                v = d[(x * 240 + 239 - y) * 2] | d[(x * 240 + 239 - y) * 2 + 1] << 8
                r += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
            rows.append(r)
        dest = p.with_suffix('.png')
        png(dest, 320, 240, rows)
        files.append(dest.name)
    (ROOT / 'build/bottom-host-test.json').write_text(json.dumps({'passed': True, 'images': files,
                                                                  'source': 'original locally extracted US 1.02 art'}, indent=2))
    print('Original-art compositor fixtures rendered:', len(files))


if __name__ == '__main__':
    main()
