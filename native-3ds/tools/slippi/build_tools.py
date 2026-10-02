"""Build the Slippi network test tools for Windows x86_64 with the llvm-mingw
clang in .toolchain (no other dependencies).

    python tools/slippi/build_tools.py            # -> build/slippi-tools/*.exe

Tools:
  slippi_fake_mm.exe    fake matchmaking server (ENet + JSON, pairs Direct tickets)
  slippi_fake_peer.exe  Dolphin-like peer (rollback pacing, 60 fps pads, acks)
  slippi_client.exe     the 3DS self-test (slippi_selftest.c) built for the PC

The client library (port/3ds/slippi, including the vendored ENet) is compiled
from the same sources as the 3DS build.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SLIPPI = ROOT / 'port/3ds/slippi'
OUT = ROOT / 'build/slippi-tools'


def clang():
    found = sorted((ROOT / '.toolchain').glob('llvm-mingw-*/bin/x86_64-w64-mingw32-clang.exe'))
    if not found:
        raise SystemExit('llvm-mingw clang not found under .toolchain')
    return str(found[-1])


def main():
    cc = clang()
    OUT.mkdir(parents=True, exist_ok=True)
    obj_dir = OUT / 'obj'
    obj_dir.mkdir(exist_ok=True)
    flags = ['-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-sign-compare',
             '-I' + str(SLIPPI), '-I' + str(SLIPPI / 'enet/include'), '-D_WIN32_WINNT=0x0601']
    lib = sorted(SLIPPI.glob('*.c'))
    enet = sorted((SLIPPI / 'enet').glob('*.c'))
    objects = []
    for src in lib + enet:
        obj = obj_dir / (('enet_' if src.parent.name == 'enet' else '') + src.stem + '.o')
        extra = ['-w'] if src.parent.name == 'enet' else []
        subprocess.run([cc, *flags, *extra, '-c', str(src), '-o', str(obj)], check=True)
        objects.append(obj)
    libs = ['-lws2_32', '-lwinmm']
    for name, src in (('slippi_fake_mm', 'fake_mm.c'), ('slippi_fake_peer', 'fake_peer.c'), ('slippi_client', 'client.c')):
        exe = OUT / (name + '.exe')
        subprocess.run([cc, *flags, str(ROOT / 'tools/slippi/src' / src), *map(str, objects), *libs, '-o', str(exe)], check=True)
        print('built', exe)


if __name__ == '__main__':
    sys.exit(main())
