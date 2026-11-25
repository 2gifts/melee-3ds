"""Build the group play test tools (PC): the same sources as the 3DS build (port/3ds/group, port/3ds/localwireless),
over the loopback link, plus the port's platform layer (port/3ds/slippi/slippi_plat.c).

    python tools/group/build_tools.py            # -> build/group-tools/

Tools:
  group_client      a console with test pads: lobby, doors, Start, stage, matches (group_test.py, group_lobby_test.py)
  lw_lobby_client   the localwireless module alone (tools/localwireless/lw_lobby_test.py)
  lobby_unit        the Melee lobby rules alone (group_lobby.c)
  auto_handicap_unit  vanilla's auto handicap rule (fn_80169000, from the decomp's gm_1601.c) for the group's inputs;
                    built only where the decomp's source is found ($MELEE_UPSTREAM_SRC, upstream/melee/src, the work
                    volume's /work/src/upstream/melee/src, or the repository this checkout sits in)

On Windows this uses the llvm-mingw clang in .toolchain; on Linux and macOS the system C compiler ($CC, else cc),
which is what the Docker test runner (docker/emu-test.sh) does.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GROUP = ROOT / 'port/3ds/group'
LW = ROOT / 'port/3ds/localwireless'
SLIPPI = ROOT / 'port/3ds/slippi'
OUT = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or ROOT / 'build/group-tools')
WINDOWS = sys.platform == 'win32'
EXE = '.exe' if WINDOWS else ''


def clang():
    if not WINDOWS:
        return os.environ.get('CC', 'cc')
    found = sorted((ROOT / '.toolchain').glob('llvm-mingw-*/bin/x86_64-w64-mingw32-clang.exe'))
    if not found:
        raise SystemExit('llvm-mingw clang not found under .toolchain')
    return str(found[-1])


def upstream_function(name):
    """A function's source text, taken from the decomp (gm_1601.c), or None if the decomp is not here."""
    places = [os.environ.get('MELEE_UPSTREAM_SRC', ''), ROOT / 'upstream/melee/src', '/work/src/upstream/melee/src',
              ROOT.parent / 'src']
    for place in places:
        path = Path(place) / 'melee/gm/gm_1601.c' if place else None
        if path and path.is_file():
            text = path.read_text(encoding='utf-8', errors='replace')
            start = text.find(f'void {name}(')
            end = text.find('\n}\n', start)
            if start >= 0 and end >= 0:
                return text[start:end + 3]
    return None


def main():
    cc = clang()
    OUT.mkdir(parents=True, exist_ok=True)
    obj_dir = OUT / 'obj'
    obj_dir.mkdir(exist_ok=True)
    flags = ['-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-sign-compare',
             '-I' + str(GROUP), '-I' + str(LW), '-I' + str(SLIPPI), '-I' + str(ROOT / 'port/include')]
    flags += ['-D_WIN32_WINNT=0x0601'] if WINDOWS else ['-D_GNU_SOURCE', '-pthread']
    libs = ['-lws2_32', '-lwinmm'] if WINDOWS else ['-pthread', '-lm']

    def compile_all(sources, prefix):
        objects = []
        for src in sources:
            obj = obj_dir / (prefix + src.stem + '.o')
            subprocess.run([cc, *flags, '-c', str(src), '-o', str(obj)], check=True)
            objects.append(obj)
        return objects

    lw = compile_all(sorted(LW.glob('*.c')) + [ROOT / 'port/3ds/name_text.c'], 'lw_')
    group = compile_all(sorted(GROUP.glob('*.c')), 'group_')
    plat = compile_all([SLIPPI / 'slippi_plat.c'], 'plat_')

    def link(name, sources, objects):
        exe = OUT / (name + EXE)
        subprocess.run([cc, *flags, *map(str, sources), *map(str, objects), *libs, '-o', str(exe)], check=True)
        print('built', exe)

    link('group_client', [ROOT / 'tools/group/src/group_client.c'], lw + group + plat)
    link('lw_lobby_client', [ROOT / 'tools/localwireless/lw_lobby_client.c'], lw)
    link('lobby_unit', [ROOT / 'tools/group/src/lobby_unit.c', GROUP / 'group_lobby.c'], [])
    rule = upstream_function('fn_80169000')
    if rule:
        gen = obj_dir / 'auto_handicap_vanilla.c'
        gen.write_text('#include "auto_handicap_unit.h"\n' + rule, encoding='utf-8')
        flags += ['-I' + str(ROOT / 'tools/group/src'), '-Wno-unused-variable']   # vanilla's own padding variable
        link('auto_handicap_unit', [ROOT / 'tools/group/src/auto_handicap_unit.c', gen], [])
    else:
        (OUT / ('auto_handicap_unit' + EXE)).unlink(missing_ok=True)
        print('auto_handicap_unit: the decomp\'s gm_1601.c is not here; not built')


if __name__ == '__main__':
    main()
