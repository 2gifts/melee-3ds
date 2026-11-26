"""The PC-console tier of group play's tests: every group_test.py scenario, the localwireless module's lobby test and the
menu art test, several at once (each on its own UDP ports and in its own folder).

    docker/emu-test.sh run tools/group/run_pc_suite.py [--jobs 6] [--only a,b]   # in the test container
    python tools/group/run_pc_suite.py                                         # anywhere the tools are built
                                                                               # (python tools/group/build_tools.py)

Prints PASS / FAIL per scenario as each ends and a summary; each scenario's output is in
$MELEE_TEST_OUT/pc-suite/<name>.log (default build/group-tests/pc-suite). The tiers: tools/test_group.py (host C, seconds),
this one (a few minutes), tools/group/run_azahar_suite.py (the emulator).
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('MELEE_TEST_OUT') or ROOT / 'build/group-tests') / 'pc-suite'

G = ['tools/group/group_test.py']
LW = ['tools/localwireless/lw_lobby_test.py']
# name, command (python script and arguments; --port is added)
SCENARIOS = [
    ('default', G),
    ('drop50', G + ['--players', '4', '--games', '2', '--drop', '50']),
    ('crash', G + ['--players', '4', '--games', '1', '--crash', '3:200']),
    ('via_menu', G + ['--players', '3', '--via-menu']),
    ('fifth', G + ['--players', '4', '--fifth']),
    ('late', G + ['--players', '3', '--games', '2', '--late', '3']),
    ('results_hold', G + ['--players', '3', '--games', '2', '--results-hold', '3:2000']),
    ('results_linger', G + ['--players', '3', '--games', '2', '--results-linger', '2:2500']),
    ('results_leave', G + ['--players', '4', '--games', '2', '--results-leave', '3']),
    ('results_leave_vanish', G + ['--players', '4', '--games', '2', '--results-leave', '3:vanish']),
    ('stale_view', G + ['--players', '3', '--games', '2', '--stale-view', '2']),
    ('lost_host_left', G + ['--lost', 'host-left']),
    ('lost_host_vanish', G + ['--lost', 'host-vanish']),
    ('last_leaves', G + ['--last-leaves']),
    ('wireless_off', G + ['--wireless-off']),
    ('rejoin', G + ['--players', '3', '--games', '2', '--frames', '900', '--rejoin', '3:200']),
    ('results_rejoin', G + ['--players', '3', '--games', '2', '--results-rejoin', '2']),
    ('cycles', G + ['--cycles', '3']),
    ('link_delay', G + ['--players', '2', '--games', '1', '--via-menu', '--link-delay', '500']),
    ('lw_lobby', LW),
    ('lw_lobby_drop30', LW + ['--drop', '30']),
    ('lw_lobby_names', LW + ['--scenario', 'names']),
    ('menu_art', ['tools/group/menu_art_test.py']),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--only', default='')
    ap.add_argument('--first-port', type=int, default=46000)
    args = ap.parse_args()
    only = set(filter(None, args.only.split(',')))
    todo = [s for s in SCENARIOS if not only or s[0] in only]
    if only - {s[0] for s in SCENARIOS}:
        print(f'unknown: {sorted(only - {s[0] for s in SCENARIOS})}')
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    running, results = [], []
    port = args.first_port
    while todo or running:
        while todo and len(running) < args.jobs:
            name, cmd = todo.pop(0)
            env = dict(os.environ, MELEE_TEST_OUT=str(OUT / name))
            extra = [] if 'menu_art_test.py' in cmd[0] else ['--port', str(port)]
            port += 100   # a scenario takes a port per console and one more per newcomer: 100 apart
            log = open(OUT / f'{name}.log', 'w')
            proc = subprocess.Popen([sys.executable, str(ROOT / cmd[0]), *cmd[1:], *extra], cwd=ROOT, env=env,
                                    stdout=log, stderr=subprocess.STDOUT)
            running.append((name, proc, log, time.time()))
        time.sleep(0.5)
        for item in list(running):
            name, proc, log, start = item
            if proc.poll() is None:
                continue
            running.remove(item)
            log.close()
            results.append((name, proc.returncode))
            print(f'{"PASS" if proc.returncode == 0 else "FAIL"} {name} ({time.time() - start:.0f} s)', flush=True)
            if proc.returncode != 0:
                for line in (OUT / f'{name}.log').read_text(errors='replace').splitlines()[-8:]:
                    print('    ' + line)
    failed = [n for n, rc in results if rc != 0]
    print(f'{len(results) - len(failed)}/{len(results)} passed in {time.time() - t0:.0f} s'
          + (f'; failed: {", ".join(failed)}' if failed else '') + f'; logs in {OUT}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
