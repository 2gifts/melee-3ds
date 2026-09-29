"""Compile-check sources after all overlays and layout fixes, with the BE8
engine flags (-fsyntax-only; no objects are written).
Usage (from native-3ds): python tools/layout_fixes/syntax.py path-suffix.c [...]"""
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build import ROOT, UPSTREAM, prepare, common_flags, local_clang
from engine_build import LIBC, INLINE_MATH
from engine_overlays import adapt

includes = prepare() + ['-isystem', str(ROOT / '.toolchain/devkitpro/devkitARM/arm-none-eabi/include')]
flags = [local_clang(), '--no-default-config', '--target=armeb-none-eabi', '-mcpu=mpcore',
         '-mfpu=vfp', '-mfloat-abi=hard', '-mtp=soft', *common_flags(),
         '-fno-builtin', '-DMP_GAME_ABI', '-Dmain=mp_engine_main', '-D__eabi=mp_engine_eabi',
         '-D__assert=mp_be_assert', '-fno-short-enums', *('-D' + n + '=mp_be_' + n for n in LIBC),
         '-fno-math-errno', *('-D' + n + '=__builtin_' + n for n in INLINE_MATH), *includes,
         '-DMP_CLAMPED_SHADE', '-Wno-everything', '-Werror=implicit-function-declaration',
         '-Werror=incompatible-pointer-types', '-Werror=int-conversion']
failed = 0
for key in sys.argv[1:]:
    matches = [p for p in (UPSTREAM / 'src').rglob(Path(key).name) if p.as_posix().endswith('/' + key)]
    if len(matches) != 1:
        print('NOT FOUND or ambiguous:', key, matches); failed += 1; continue
    source = adapt(matches[0])
    result = subprocess.run([*flags, '-I' + str(matches[0].parent), '-fsyntax-only', str(source)],
                            capture_output=True, text=True, errors='replace')
    print(('ok ' if not result.returncode else 'ERROR ') + key)
    if result.returncode:
        failed += 1; print(result.stderr[-4000:])
sys.exit(1 if failed else 0)
