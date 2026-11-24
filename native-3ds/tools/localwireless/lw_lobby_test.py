"""Tests of the local wireless module alone (port/3ds/localwireless), no game: several lw_lobby_client processes
over the loopback UDP link, joining and leaving at different times.

    python tools/localwireless/lw_lobby_test.py [--scenario all|alone|churn] [--drop 40]
alone: the host sits in the lobby by itself and plays a one-console game, twice.
churn: console 3 joins and leaves, another takes its slot, console 4 joins in the middle of game 1 (and must wait), game 2
       has everybody, and the console in slot 3 vanishes without a goodbye in the middle of it. Every console that stays must see
       identical bundles for each game, no lobby blob may be torn or go back, and every command must arrive in order.
Build first: python tools/group/build_tools.py.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or ROOT / 'build/group-tools')
OUT = Path(os.environ.get('MELEE_TEST_OUT') or ROOT / 'build/group-tests') / 'lw_lobby'
EXE = '.exe' if sys.platform == 'win32' else ''


def run(name, port, drop, procs):
    """procs: list of (label, args); started in order 0.15 s apart. Returns {label: (code, lines)}."""
    running = {}
    for label, args in procs:
        log = open(OUT / f'{name}_{label}.log', 'w')
        cmd = [str(TOOLS / ('lw_lobby_client' + EXE)), '--port', str(port), '--drop', str(drop)] + [str(a) for a in args]
        running[label] = (subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log)
        time.sleep(0.15)
    end = time.monotonic() + 90
    result = {}
    for label, (p, log) in running.items():
        try:
            code = p.wait(max(1, end - time.monotonic()))
        except subprocess.TimeoutExpired:
            p.kill()
            code = 'timeout'
        log.close()
        result[label] = (code, (OUT / f'{name}_{label}.log').read_text(errors='replace').splitlines())
    return result


def lines(res, label, prefix):
    return [l.split(prefix, 1)[1].strip() for l in res[label][1] if l.split('] ', 1)[-1].startswith(prefix)]


def check(name, ok, why=''):
    print(('PASS ' if ok else 'FAIL ') + name + (': ' + why if why and not ok else ''))
    return ok


def alone(port, drop):
    res = run('alone', port, drop, [('host', ['--host', '--games', 2, '--start-at', 1000, '--frames', 120, '--run-ms', 7000])])
    m = lines(res, 'host', 'MATCH')
    return [check('alone: host exits 0', res['host'][0] == 0), check('alone: two games played alone', len(m) == 2, str(m))]


def churn(port, drop):
    # Times in ms from the start of each process; --join-at delays the process's own start.
    res = run('churn', port, drop, [
        ('host', ['--host', '--games', 2, '--start-at', 3500, '--frames', 240, '--run-ms', 14000]),
        ('c2', ['--join', '--join-at', 500]),
        ('c3', ['--join', '--join-at', 800, '--leave-at', 2300]),
        ('c3b', ['--join', '--join-at', 2600, '--crash-at', 11000]),  # takes the slot c3 left; vanishes in game 2
        ('c4', ['--join', '--join-at', 5000]),                       # arrives during game 1
    ])
    ok = True
    for label in ('host', 'c2', 'c3b', 'c4'):
        ok &= check(f'churn: {label} exits 0', res[label][0] == 0, str(res[label][0]))
    ok &= check('churn: c3 left cleanly', res['c3'][0] == 0)
    g = {label: {l.split()[0]: l.split()[1] for l in lines(res, label, 'MATCH')} for label in ('host', 'c2', 'c3b', 'c4')}
    ok &= check('churn: game 1 hash identical on host, c2, c3b', len({g[x].get('1') for x in ('host', 'c2', 'c3b')}) == 1 and bool(g['host'].get('1')), str(g))
    ok &= check('churn: c4 did not play game 1', '1' not in g['c4'], str(g['c4']))
    ok &= check('churn: c4 saw the game in progress', bool(lines(res, 'c4', 'LATE')))
    ok &= check('churn: game 2 hash identical on the three that stayed', len({g[x].get('2') for x in ('host', 'c2', 'c4')}) == 1 and bool(g['host'].get('2')), str(g))
    host_cmds = {l.split()[0]: l.split()[1] for l in lines(res, 'host', 'CMDS')}
    for label, idx in (('c2', '1'), ('c4', '3')):
        sent = lines(res, label, 'CMDS')
        ok &= check(f'churn: every command from {label} arrived', bool(sent) and sent[0].split()[1] == host_cmds.get(idx), f'{sent} vs {host_cmds}')
    finals = {label: lines(res, label, 'FINAL')[0] for label in ('host', 'c2', 'c4')}
    ok &= check('churn: identical final present mask', len(set(finals.values())) == 1, str(finals))
    return [ok]


def again(port, drop):
    # A follow-up game (lw_host_start_again, e.g. a sudden death) has only the consoles of the game before it: c3, who
    # joined during game 1, must not play game 2 either, while host and c2 do.
    res = run('again', port, drop, [
        ('host', ['--host', '--again', '--games', 2, '--start-at', 2500, '--frames', 240, '--run-ms', 13000]),
        ('c2', ['--join', '--join-at', 500]),
        ('c3', ['--join', '--join-at', 4000]),                       # arrives during game 1
    ])
    ok = True
    for label in ('host', 'c2', 'c3'):
        ok &= check(f'again: {label} exits 0', res[label][0] == 0, str(res[label][0]))
    g = {label: {l.split()[0]: l.split()[1] for l in lines(res, label, 'MATCH')} for label in ('host', 'c2', 'c3')}
    ok &= check('again: host and c2 played both games alike', all(g['host'].get(k) and g['host'].get(k) == g['c2'].get(k) for k in ('1', '2')), str(g))
    ok &= check('again: c3 played neither', not g['c3'], str(g['c3']))
    return [ok]


def browse(port, drop):
    # Two hosts in range (ports base+1 and base+3, different beacons); a browser joins the second one by its beacon.
    res = run('browse', port, drop, [
        ('h1', ['--host', '--port', port + 1, '--beacon-tag', 11, '--run-ms', 7000, '--games', 0]),
        ('h2', ['--host', '--port', port + 3, '--beacon-tag', 22, '--run-ms', 7000, '--games', 0]),
        ('b', ['--join', '--want-tag', 22, '--join-at', 1000]),
    ])
    b = res['b'][1]
    joined = [l for l in b if 'joining #' in l]
    ok = check('browse: both hosts exit 0', res['h1'][0] == 0 and res['h2'][0] == 0)
    ok &= check('browse: the browser saw two games and joined the one it wanted', bool(joined) and 'tag 22' in joined[0] and joined[0].split('SCAN: ')[1].startswith('2 games'), str(joined))
    ok &= check('browse: the browser ended in the second host\'s lobby, not the first', res['b'][0] == 0 and any('FINAL' in l for l in b) and not any('FINAL' in l for l in []))
    h2 = [l for l in res['h2'][1] if 'joined the lobby' in l]
    h1 = [l for l in res['h1'][1] if 'joined the lobby' in l]
    ok &= check('browse: joined host 2 only', len(h2) == 1 and not h1, f'{h1} {h2}')
    return [ok]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scenario', default='all')
    ap.add_argument('--drop', type=int, default=0)
    ap.add_argument('--port', type=int, default=47030)
    args = ap.parse_args()
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    results = []
    if args.scenario in ('all', 'alone'):
        results += alone(args.port, args.drop)
    if args.scenario in ('all', 'browse'):
        results += browse(args.port + 10, args.drop)
    if args.scenario in ('all', 'churn'):
        results += churn(args.port + 1, args.drop)
    if args.scenario in ('all', 'again'):
        results += again(args.port + 20, args.drop)
    print('RESULT:', 'PASS' if all(results) else 'FAIL', '->', OUT)
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
