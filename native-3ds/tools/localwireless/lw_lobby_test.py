"""Tests of the local wireless module alone (port/3ds/localwireless), no game: several lw_lobby_client processes
over the loopback UDP link, joining and leaving at different times.

    python tools/localwireless/lw_lobby_test.py [--scenario all|alone|browse|churn|again|menu|menujoin|cycles] [--drop 40]
browse: two hosts in range; a browser waits until both are listed (--want-games 2) and joins the one it wants.
alone: the host sits in the lobby by itself and plays a one-console game, twice.
churn: console 3 joins and leaves, another takes its slot, console 4 joins in the middle of game 1 (and must wait), game 2
       has everybody, and the console in slot 3 vanishes without a goodbye in the middle of it. Every console that stays must see
       identical bundles for each game, no lobby blob may be torn or go back, and every command must arrive in order.
menu: menu input (the camera's C-stick): every console's value reaches the others in time, a console that vanishes
       while tilted reads neutral soon, one that leaves at once, nothing is sent during a game and a held value comes
       back after it.
menujoin: a console that joins while another holds a tilt reads it soon after arriving.
names: a console named with 10 kana and one with accented letters join: every roster shows both whole.
cycles: c2 stays throughout while five short-lived consoles come and go one after another (one vanishes), some during
       games: each gets port 3 (index 2), the host and c2 agree on every game, no blob is torn or goes back, and every
       command arrives in order.
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
    # The browser waits (--want-games 2, up to 3 s) until both games are listed before it joins, so under loss the
    # count it prints is not a race with the first host's beacon.
    res = run('browse', port, drop, [
        ('h1', ['--host', '--port', port + 1, '--beacon-tag', 11, '--run-ms', 10000, '--games', 0]),
        ('h2', ['--host', '--port', port + 3, '--beacon-tag', 22, '--run-ms', 10000, '--games', 0]),
        ('b', ['--join', '--want-tag', 22, '--want-games', 2, '--join-at', 1000]),
    ])
    b = res['b'][1]
    joined = [l for l in b if 'joining #' in l]
    ok = check('browse: both hosts exit 0', res['h1'][0] == 0 and res['h2'][0] == 0)
    ok &= check('browse: the browser saw two games before joining', bool(joined) and joined[0].split('SCAN: ')[1].startswith('2 games'), str(joined))
    ok &= check('browse: it joined the one it wanted (tag 22)', bool(joined) and 'tag 22' in joined[0], str(joined))
    ok &= check('browse: the browser ended in the second host\'s lobby, not the first', res['b'][0] == 0 and any('FINAL' in l for l in b) and not any('FINAL' in l for l in []))
    h2 = [l for l in res['h2'][1] if 'joined the lobby' in l]
    h1 = [l for l in res['h1'][1] if 'joined the lobby' in l]
    ok &= check('browse: joined host 2 only', len(h2) == 1 and not h1, f'{h1} {h2}')
    return [ok]


def cycles(port, drop):
    # Five consoles take turns on the same port while c2 stays: joins and leaves in the lobby and during games, one
    # vanishing without a goodbye. The windows do not overlap, so each newcomer gets the port the last one left.
    # (the one after the vanishing console comes 2.8 s later: its port is free once the 2 s drop timeout has passed)
    windows = [(500, ['--leave-at', 2600]), (3000, ['--leave-at', 5000]), (5500, ['--crash-at', 7000]),
               (9800, ['--leave-at', 11500]), (12000, ['--leave-at', 13500])]
    procs = [('host', ['--host', '--games', 3, '--start-at', 2000, '--frames', 120, '--run-ms', 15000]),
             ('c2', ['--join', '--join-at', 200])]
    for k, (at, end) in enumerate(windows, 1):
        # --join-at and --leave-at count from the process's own start, which is 0.15 s per process later
        start = 0.15 * (k + 1) * 1000
        procs.append((f'j{k}', ['--join', '--join-at', int(at - start)] + [end[0], int(end[1] - start)]))
    res = run('cycles', port, drop, procs)
    ok = True
    for label in res:
        ok &= check(f'cycles: {label} exits 0', res[label][0] == 0, str(res[label][0]))
    g = {label: {l.split()[0]: l.split()[1] for l in lines(res, label, 'MATCH')} for label in ('host', 'c2')}
    ok &= check('cycles: host and c2 played all three games alike',
                all(g['host'].get(k) and g['host'].get(k) == g['c2'].get(k) for k in ('1', '2', '3')), str(g))
    idx = {label: [l.split('console index ')[1].split(',')[0] for l in res[label][1] if 'console index ' in l]
           for label in res if label.startswith('j')}
    ok &= check('cycles: every newcomer got port 3 (index 2)', all(v == ['2'] for v in idx.values()), str(idx))
    host_cmds = {l.split()[0]: l.split()[1] for l in lines(res, 'host', 'CMDS')}
    sent = lines(res, 'c2', 'CMDS')
    ok &= check('cycles: every command from c2 arrived in order', bool(sent) and sent[0].split()[1] == host_cmds.get('1'),
                f'{sent} vs {host_cmds}')
    bad = {label: [l for l in res[label][1] if 'FAIL' in l] for label in res}
    ok &= check('cycles: no torn or backwards blob, no out-of-order command', not any(bad.values()), str(bad))
    return [ok]


def names(port, drop):
    # Names: a console named with 10 kana (30 bytes of UTF-8) and one with accents join; every roster shows each name
    # whole (the lobby header carries 32 bytes per name).
    kana = '\u3086\u3046\u304d' * 3 + '\u3086'   # ゆうきゆうきゆうきゆ
    res = run('names', port, drop, [
        ('host', ['--host', '--games', 0, '--run-ms', 4000, '--name', 'Anna']),
        ('c2', ['--join', '--join-at', 200, '--name', kana]),
        ('c3', ['--join', '--join-at', 400, '--name', 'Zo\u00eb \u00c5ngstr\u00f6m']),
    ])
    ok = check('names: every console exits 0', all(res[x][0] == 0 for x in res), str({x: res[x][0] for x in res}))
    want = {'Anna', kana, 'Zo\u00eb \u00c5ngstr'}   # 10 characters
    for label in res:
        got = {l.split(' ', 1)[1] for l in lines(res, label, 'NAME ')}
        ok &= check(f'names: {label} shows every name whole', got == want, f'{got}')
    return [ok]


def menu_reads(res, label):
    """[(port, value, clock ms)] from a console's MENU lines."""
    return [tuple(int(x) for x in l.split()) for l in lines(res, label, 'MENU ')]


def clock_of(res, label, text):
    found = [l for l in res[label][1] if text in l and '(clock ' in l]
    return int(found[0].split('(clock ')[1].rstrip(')')) if found else None


def first_read(res, label, port, value, after):
    """When `label` first read `value` for `port` at or after the clock `after` (None: never)."""
    for p, v, at in menu_reads(res, label):
        if p == port and v == value and at >= after:
            return at
    return None


def menu(port, drop):
    # Menu input (the C-stick camera on the CSS and SSS): each console's value reaches every other console; a console
    # that vanishes while tilted reads neutral soon (no refresh keeps it), one that leaves at once; nothing is sent
    # while a game runs, and a value held through it comes back afterwards.
    limit = 150 if not drop else 1000
    res = run('menu', port, drop, [
        ('host', ['--host', '--games', 1, '--start-at', 5000, '--frames', 120, '--run-ms', 10000, '--menu-log',
                  '--menu', '20@1000', '--menu', '0@1600']),
        ('c2', ['--join', '--join-at', 200, '--menu-log', '--menu', '50@1500', '--menu', '0@2200', '--menu', '60@4000']),
        ('c3', ['--join', '--join-at', 350, '--menu-log', '--menu', '70@2500', '--crash-at', 3300]),
        ('c4', ['--join', '--join-at', 500, '--menu-log', '--menu', '30@2600', '--leave-at', 3400]),
    ])
    ok = True
    for label in ('host', 'c2'):
        ok &= check(f'menu: {label} exits 0', res[label][0] == 0, str(res[label][0]))
    ports = {label: int(l.split('console index ')[1].split(',')[0]) for label in ('host', 'c2', 'c3', 'c4')
             for l in res[label][1] if 'console index ' in l}

    def value_at(label, port_, at):
        v = 0
        for p, val, t in menu_reads(res, label):
            if p == port_ and t <= at:
                v = val
        return v

    def spread(setter, value, readers, after=0, what=''):
        port_ = ports.get(setter)
        set_at = first_read(res, setter, port_, value, after)
        late = {}
        for r in readers:
            if value == 0:   # back to neutral: what it reads by then (under loss it may have read neutral earlier)
                late[r] = limit if set_at and value_at(r, port_, set_at + limit) == 0 else None
                continue
            at = first_read(res, r, port_, value, set_at or 0) if set_at else None
            late[r] = None if at is None else at - set_at
        good = set_at is not None and all(v is not None and v <= limit for v in late.values())
        return check(f'menu: {what or f"{setter} sets {value}"}: every other console reads it within {limit} ms', good,
                     f'set at {set_at}, delays {late}'), set_at
    r, _ = spread('host', 20, ['c2', 'c3', 'c4'])
    ok &= r
    r, t50 = spread('c2', 50, ['host', 'c3', 'c4'])
    ok &= r
    r, _ = spread('c2', 0, ['host', 'c3', 'c4'], (t50 or 0) + 1, 'c2 back to neutral')
    ok &= r
    # c3 vanishes holding 70: neutral within the staleness timeout (350 ms) and a refresh or two, long before it is
    # known to be gone.
    def stays_neutral(port_, since, within):
        # neutral by since + within and never anything else after (under loss it may read neutral earlier: a value
        # with no refresh for 350 ms is stale)
        return {r: value_at(r, port_, since + within) == 0 and all(
            v == 0 for p, v, t in menu_reads(res, r) if p == port_ and t > since + within) for r in ('host', 'c2')}
    crash = clock_of(res, 'c3', 'vanishing at')
    stale = 350 + 150 + (limit if drop else 0)
    neutral = stays_neutral(ports.get('c3'), crash or 0, stale)
    ok &= check(f'menu: a console that vanishes while tilted reads neutral within {stale} ms',
                crash is not None and all(neutral.values()), f'vanished at {crash}: {neutral}')
    left = clock_of(res, 'c4', 'leaving at')
    neutral = stays_neutral(ports.get('c4'), left or 0, limit)
    ok &= check(f'menu: a console that leaves reads neutral within {limit} ms',
                left is not None and all(neutral.values()), f'left at {left}: {neutral}')
    tx = {label: [int(l.split()[1]) for l in lines(res, label, 'MENUTX')] for label in ('host', 'c2')}
    ok &= check('menu: no menu packet is sent while the game runs', all(v == [0] for v in tx.values()), str(tx))
    # c2 holds 60 from before the game until the end: the host reads it, then neutral while the game runs (nothing is
    # sent or read then), then 60 again.
    reads = [v for p, v, _ in menu_reads(res, 'host') if p == ports.get('c2')]
    held = [i for i, v in enumerate(reads) if v == 60]
    ok &= check('menu: a value held through the game reads neutral during it and comes back after it',
                len(held) >= 2 and 0 in reads[held[0]:held[-1]], str(reads))
    return [ok]


def menu_join(port, drop):
    # A console joins while another holds a tilt: the 100 ms refresh brings it the held value soon after it arrives.
    limit = 250 if not drop else 1500
    res = run('menujoin', port, drop, [
        ('host', ['--host', '--games', 0, '--run-ms', 5000, '--menu-log']),
        ('c2', ['--join', '--join-at', 200, '--menu-log', '--menu', '45@800']),
        ('c3', ['--join', '--join-at', 2500, '--menu-log']),
    ])
    ok = check('menu join: every console exits 0', all(res[x][0] == 0 for x in ('host', 'c2', 'c3')),
               str({x: res[x][0] for x in res}))
    c2 = int([l for l in res['c2'][1] if 'console index ' in l][0].split('console index ')[1].split(',')[0])
    arrived = clock_of(res, 'c3', 'console index')
    got = first_read(res, 'c3', c2, 45, 0)
    ok &= check(f'menu join: a console that joins during a held tilt reads it within {limit} ms of arriving',
                arrived is not None and got is not None and got - arrived <= limit, f'arrived {arrived}, read at {got}')
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
    if args.scenario in ('all', 'menu'):
        results += menu(args.port + 30, args.drop)
    if args.scenario in ('all', 'names'):
        results += names(args.port + 60, args.drop)
    if args.scenario in ('all', 'cycles'):
        results += cycles(args.port + 50, args.drop)
    if args.scenario in ('all', 'menujoin'):
        results += menu_join(args.port + 40, args.drop)
    print('RESULT:', 'PASS' if all(results) else 'FAIL', '->', OUT)
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
