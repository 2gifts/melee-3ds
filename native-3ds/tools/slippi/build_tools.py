"""Build the Slippi network test tools.

On Windows this uses the llvm-mingw clang in .toolchain (no other dependencies).
On Linux and macOS it uses the system C compiler ($CC, else cc), which is
what the Docker test runner (docker/emu-test.sh) does.

    python tools/slippi/build_tools.py            # -> build/slippi-tools/*[.exe]

Tools:
  slippi_fake_mm.exe    fake matchmaking server (ENet + JSON, pairs Direct tickets)
  slippi_fake_peer.exe  Dolphin-like peer (rollback pacing, 60 fps pads, acks)
  slippi_client.exe     the 3DS self-test (slippi_selftest.c) built for the PC

The client library (port/3ds/slippi, including the vendored ENet) is compiled
from the same sources as the 3DS build.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SLIPPI = ROOT / 'port/3ds/slippi'
LW = ROOT / 'port/3ds/localwireless'
OUT = Path(os.environ.get('MELEE_TOOLS_DIR') or ROOT / 'build/slippi-tools')


WINDOWS = sys.platform == 'win32'
EXE = '.exe' if WINDOWS else ''


# What ENet's autoconf would define for a POSIX host (unix.c is not self-detecting).
ENET_FEATURES = [] if WINDOWS else ['-D' + n for n in (
    'HAS_FCNTL', 'HAS_POLL', 'HAS_GETADDRINFO', 'HAS_GETNAMEINFO', 'HAS_INET_PTON', 'HAS_INET_NTOP',
    'HAS_MSGHDR_FLAGS', 'HAS_SOCKLEN_T')]


def clang():
    if not WINDOWS:
        return os.environ.get('CC', 'cc')
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
             '-I' + str(SLIPPI), '-I' + str(SLIPPI / 'enet/include'), '-I' + str(LW)]
    flags += ['-D_WIN32_WINNT=0x0601'] if WINDOWS else ['-D_GNU_SOURCE', '-pthread']
    # slippi_files.c, slippi_boot_ui.c, slippi_prefetch.c and slippi_ui.c are 3DS only.
    lib = sorted(p for p in SLIPPI.glob('*.c') if p.name not in ('slippi_files.c', 'slippi_boot_ui.c', 'slippi_prefetch.c', 'slippi_ui.c'))
    enet = sorted((SLIPPI / 'enet').glob('*.c'))
    lw = sorted(LW.glob('*.c'))
    objects = []
    lw_objects = []
    for src in lw:
        obj = obj_dir / ('lw_' + src.stem + '.o')
        subprocess.run([cc, *flags, '-c', str(src), '-o', str(obj)], check=True)
        objects.append(obj)
        lw_objects.append(obj)
    for src in lib + enet:
        obj = obj_dir / (('enet_' if src.parent.name == 'enet' else '') + src.stem + '.o')
        extra = ['-w', *ENET_FEATURES] if src.parent.name == 'enet' else []
        subprocess.run([cc, *flags, *extra, '-c', str(src), '-o', str(obj)], check=True)
        objects.append(obj)
    libs = ['-lws2_32', '-lwinmm'] if WINDOWS else ['-pthread', '-lm']
    for name, src in (('slippi_fake_mm', 'fake_mm.c'), ('slippi_fake_peer', 'fake_peer.c'), ('slippi_client', 'client.c')):
        exe = OUT / (name + EXE)
        subprocess.run([cc, *flags, str(ROOT / 'tools/slippi/src' / src), *map(str, objects), *libs, '-o', str(exe)], check=True)
        print('built', exe)
    exe = OUT / ('lw_client' + EXE)   # the module alone (tools/localwireless)
    subprocess.run([cc, *flags, str(ROOT / 'tools/localwireless/lw_client.c'), *map(str, lw_objects), *libs, '-o', str(exe)], check=True)
    print('built', exe)


if __name__ == '__main__':
    sys.exit(main())
