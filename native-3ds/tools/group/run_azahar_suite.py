"""The emulator tier of group play's tests: the Azahar tests that need the real game on an emulated 3DS, several at once.

    python3 tools/group/run_azahar_suite.py [--cap 6] [--only smoke,mii]     # on the host, next to Docker

Each test runs in its own test container (docker/emu-test.sh run ...), so the emulators of one never meet another's.
Tests start while the emulators running stay at --cap or fewer (default 6: the Docker VM's 8 CPUs less room for Docker
and the host); group_scan_test and group_link_test, which measure frame times, run alone. Results: each test's output in
$MELEE_TEST_OUT/azahar-suite/<name>.log and its own folder there (default native-3ds/build/emu-test/azahar-suite).
MELEE_CFL_RES (your console's CFL_Res.dat) and MELEE_CFL_DB (its Mii files) are passed on as docker/emu-test.sh takes
them: group_mii_test renders real Mii heads only with them.

The tiers: tools/test_group.py (host C, seconds), tools/group/run_pc_suite.py (PC consoles, minutes), this one.
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('MELEE_TEST_OUT') or ROOT / 'build/emu-test') / 'azahar-suite'
ALONE = 99
# name, emulators (ALONE: no other test meanwhile), the script and its arguments
TESTS = [
    ('smoke', 2, ['tools/group/group_smoke_test.py']),
    # console 3 pauses, then drops while paused: the others make it a CPU, unpause for it and reach the host's end
    ('quad', 3, ['tools/group/group_quad_test.py', '--players', '3', '--end-frame', '1500', '--pause-kill', '3:300',
                 '--timeout-ms', '20000']),
    ('lobby', 1, ['tools/group/group_lobby_test.py']),
    ('mii', 1, ['tools/group/group_mii_test.py']),
    ('mii_no_mii', 1, ['tools/group/group_mii_test.py', '--no-mii']),
    ('scan', ALONE, ['tools/group/group_scan_test.py']),
    ('link', ALONE, ['tools/group/group_link_test.py']),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cap', type=int, default=6)
    ap.add_argument('--only', default='')
    args = ap.parse_args()
    only = set(filter(None, args.only.split(',')))
    if only - {t[0] for t in TESTS}:
        print(f'unknown: {sorted(only - {t[0] for t in TESTS})}; known: {", ".join(t[0] for t in TESTS)}')
        return 2
    todo = [t for t in TESTS if not only or t[0] in only]
    todo.sort(key=lambda t: (t[1] == ALONE, -t[1]))   # the biggest first, the ones that run alone last
    OUT.mkdir(parents=True, exist_ok=True)
    running, results = [], []
    t0 = time.time()

    def load():
        return sum(min(t[1], args.cap) for t, _, _, _ in running)

    while todo or running:
        for item in list(running):
            test, proc, log, start = item
            if proc.poll() is None:
                continue
            running.remove(item)
            log.close()
            results.append((test[0], proc.returncode))
            print(f'{"PASS" if proc.returncode == 0 else "FAIL"} {test[0]} ({time.time() - start:.0f} s)', flush=True)
            if proc.returncode != 0:
                for line in (OUT / f'{test[0]}.log').read_text(errors='replace').splitlines()[-10:]:
                    print('    ' + line)
        for test in list(todo):
            alone = test[1] == ALONE or any(t[1] == ALONE for t, _, _, _ in running)
            if running and (alone or load() + min(test[1], args.cap) > args.cap):
                continue
            todo.remove(test)
            env = dict(os.environ, MELEE_TEST_OUT=str(OUT / test[0]))
            log = open(OUT / f'{test[0]}.log', 'w')
            proc = subprocess.Popen([str(ROOT / 'docker/emu-test.sh'), 'run', *test[2]], cwd=ROOT, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            running.append((test, proc, log, time.time()))
            break
        time.sleep(2)
    failed = [n for n, rc in results if rc != 0]
    print(f'{len(results) - len(failed)}/{len(results)} passed in {time.time() - t0:.0f} s'
          + (f'; failed: {", ".join(failed)}' if failed else '') + f'; logs in {OUT}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
