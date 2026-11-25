"""Host tests for the name converter (port/3ds/name_text.c) and the generated glyph table.

    python tools/test_name_text.py
Regenerates port/3ds/name_glyphs.h into build/ and fails if the checked-in one differs (tools/gen_name_glyphs.py), then
builds and runs tests/name_text_tests.c with the system C compiler ($CC, else cc)."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    fresh = build / 'name_glyphs.h'
    subprocess.run([sys.executable, str(ROOT / 'tools/gen_name_glyphs.py'), '--out', str(fresh)], check=True)
    if fresh.read_bytes() != (ROOT / 'port/3ds/name_glyphs.h').read_bytes():
        print('name_glyphs.h: FAIL the checked-in table differs from tools/gen_name_glyphs.py\'s output')
        return 1
    exe = build / 'name-text-tests'
    cc = os.environ.get('CC', 'cc')
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2', '-fsanitize=address,undefined',
                    str(ROOT / 'tests/name_text_tests.c'), str(ROOT / 'port/3ds/name_text.c'), '-o', str(exe)], check=True)
    return subprocess.run([str(exe)]).returncode


if __name__ == '__main__':
    sys.exit(main())
