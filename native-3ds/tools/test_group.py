"""Host tests for group play: every pure rule, in seconds, with AddressSanitizer and UndefinedBehaviorSanitizer.

    python3 tools/test_group.py              # build and run them all; prints one line each and a summary
    python3 tools/test_group.py css menu     # only these

Each test is one C file in tests/ built with the sources it tests, with the system C compiler ($CC, else cc):

  group_css      the character select's rules (port/engine/group/css_rules.c)
  group_sss      the stage select's (sss_rules.c)
  group_results  the results screen's and after it: presses, stars, auto handicap, names (results_rules.c)
  group_menu     Melee -> Solo / Group, the session's pages, leaving, a lost session, HOME and sleep, toasts
                 (port/3ds/group/group_menu.c over a fake session), and the menus around the lobby (menu_rules.c)
  group_lobby    the lobby the host keeps: doors, edit rights, Start, the stage pick, the results (group_lobby.c)
  auto_handicap  vanilla's auto handicap rule, taken from the decomp's gm_1601.c (skipped when the decomp is not
                 found: $MELEE_UPSTREAM_SRC, upstream/melee/src, the work volume's, or the repository around this one)

The next tier is the PC consoles (tools/group/run_pc_suite.py), then the emulator (tools/group/run_azahar_suite.py).
"""
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/group-host-tests'
GROUP = ROOT / 'port/3ds/group'
ENGINE = ROOT / 'port/engine/group'
INCLUDES = ['-I' + str(ROOT / 'port/include'), '-I' + str(GROUP), '-I' + str(ROOT / 'port/3ds/localwireless'),
            '-I' + str(ROOT / 'port/3ds/slippi'), '-I' + str(ROOT / 'tests')]
FLAGS = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-D_GNU_SOURCE',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer']

TESTS = {
    'group_css': [ROOT / 'tests/group_css_tests.c', ENGINE / 'css_rules.c'],
    'group_sss': [ROOT / 'tests/group_sss_tests.c', ENGINE / 'sss_rules.c'],
    'group_results': [ROOT / 'tests/group_results_tests.c', ENGINE / 'results_rules.c'],
    'group_menu': [ROOT / 'tests/group_menu_tests.c', GROUP / 'group_menu.c', ENGINE / 'menu_rules.c'],
    'group_lobby': [ROOT / 'tests/group_lobby_tests.c', GROUP / 'group_lobby.c'],
    'auto_handicap': [ROOT / 'tests/auto_handicap_tests.c'],
}


def upstream_function(name):
    """A function's source text, taken from the decomp's gm_1601.c, or None if the decomp is not here."""
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


def run(name):
    """(name, status, seconds, output): status is ok, FAIL or skipped."""
    t0 = time.time()
    sources = list(TESTS[name])
    flags = list(FLAGS)
    if name == 'auto_handicap':
        rule = upstream_function('fn_80169000')
        if rule is None:
            return name, 'skipped', 0.0, "the decomp's gm_1601.c is not here"
        gen = OUT / 'auto_handicap_vanilla.c'
        gen.write_text('#include "auto_handicap_tests.h"\n' + rule, encoding='utf-8')
        sources.append(gen)
        flags.append('-Wno-unused-variable')   # vanilla's own padding variable
    exe = OUT / name
    cc = os.environ.get('CC', 'cc')
    build = subprocess.run([cc, *flags, *INCLUDES, *map(str, sources), '-o', str(exe)], capture_output=True, text=True)
    if build.returncode != 0:
        return name, 'FAIL', time.time() - t0, 'build failed:\n' + build.stdout + build.stderr
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
    result = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=60)
    output = (result.stdout + result.stderr).strip()
    return name, 'ok' if result.returncode == 0 else 'FAIL', time.time() - t0, output


def main(argv):
    wanted = [a if a in TESTS else 'group_' + a for a in argv] or list(TESTS)
    unknown = [w for w in wanted if w not in TESTS]
    if unknown:
        print(f'unknown test(s): {", ".join(unknown)}; known: {", ".join(TESTS)}')
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(run, wanted))
    for name, status, seconds, output in results:
        print(f'{status:7} {name} ({seconds:.1f} s)')
        if status == 'FAIL' or (status == 'skipped' and output):
            for line in output.splitlines()[-30:]:
                print('    ' + line)
    failed = [r[0] for r in results if r[1] == 'FAIL']
    ran = sum(1 for r in results if r[1] != 'skipped')
    print(f'{ran - len(failed)}/{ran} passed in {time.time() - t0:.1f} s' + (f'; failed: {", ".join(failed)}' if failed else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
