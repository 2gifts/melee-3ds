"""Group play on PC consoles (group_client, the 3DS's group code over the loopback link): a host and 1..3 joiners meet in
a lobby, fill their doors (the host adds a CPU on a free port), start, pick a stage and play matches with test pads.
Every console that stays must report the same bundle hash for every game. The lobby rules' unit test (lobby_unit) and
the auto handicap rule's (auto_handicap_unit, where the decomp's source is available) run first.

    python tools/group/group_test.py [--players 3] [--games 2] [--drop 30] [--via-menu] [--crash 3:200]
--via-menu   meet through the Melee -> Group pages (group_menu.c) instead of joining directly
--crash N:F  console N (2..4) vanishes without a goodbye at frame F of game 1: the others make it a CPU at the same
             frame and carry on (use --games 1: the game afterwards would wait for a console that is gone)
--fifth      after 4 consoles are in the lobby, a fifth tries to join: it must see the game as full and be refused
--late N     console N (2..4) joins only once game 1 is running: it must watch that match's status (the Match in
             progress page's data), not play it, then wait through the results, and play games 2.. with everyone
             (needs --games 2 or more)
--results-hold N:MS    console N presses ready on the results screen only after MS ms: no console's results may end
             before that press
--results-linger N:MS  console N stays on the results screen (busy) MS ms after the others are back: the host's Start
             must be refused meanwhile, and the next match may not begin before N is back (needs --games 2 or more)
--stale-view N   console N (a joiner) keeps its lobby view from before the host's Start until 1.5 s after game 1
             (as if every snapshot in between was lost): its results screen starts on a character select view from
             before the match, which must not end it; it must still press ready and every console's results end
--results-leave N[:vanish]  console N leaves on game 1's results screen without pressing ready (with a goodbye, or
             vanishing): the others' results end and they play on
--lost host-left|host-vanish  (2 consoles, via the menu, 1 game) the host leaves after the game with a goodbye, or
             vanishes: the joiner's Host/Join page must show "The host left" / "The host stopped responding"
--last-leaves  (2 consoles, via the menu, 1 game) the joiner leaves after the game; the host lingers 3 s and must not
             see its session as lost
--wireless-off  one console with test_wireless_off=1 tries Melee -> Group twice: both fail with "Please enable wireless
             and try again", and the page stays on Solo / Group
--rejoin N:F  console N vanishes at frame F of game 1 and comes back (a new process with its name) while that match
             still runs: it must get port N back, watch the match, and play games 2.. with everyone (--games 2+)
--results-rejoin N  console N leaves on game 1's results screen; a new process with its name joins once it has left
             and plays games 2.. with everyone (--games 2+)
--cycles K   console 2 leaves after each game on its results screen and a fresh console takes port 2 for the next,
             K times (games = K + 1); the host and console 3 play every game; each newcomer starts with the host's
             door for port 2, not one of its own (--players 3)

Build the tools first: python tools/group/build_tools.py (docker/emu-test.sh group ... does both).
Logs: build/group-tests/<label>/*.log
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
OUT = Path(os.environ.get('MELEE_TEST_OUT') or ROOT / 'build/group-tests')
EXE = '.exe' if sys.platform == 'win32' else ''


def tail(path, pattern, n):
    lines = [l for l in path.read_text(encoding='utf-8', errors='replace').splitlines() if pattern in l]
    return '\n'.join(lines[-n:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--players', type=int, default=3)
    ap.add_argument('--games', type=int, default=2)
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--delay', type=int, default=2)
    ap.add_argument('--drop', type=int, default=0)
    ap.add_argument('--via-menu', action='store_true')
    ap.add_argument('--crash', default='')
    ap.add_argument('--late', type=int, default=0)
    ap.add_argument('--fifth', action='store_true')
    ap.add_argument('--results-hold', default='')
    ap.add_argument('--results-linger', default='')
    ap.add_argument('--results-leave', default='')
    ap.add_argument('--stale-view', type=int, default=0)
    ap.add_argument('--lost', default='', choices=['', 'host-left', 'host-vanish'])
    ap.add_argument('--last-leaves', action='store_true')
    ap.add_argument('--wireless-off', action='store_true')
    ap.add_argument('--rejoin', default='')
    ap.add_argument('--results-rejoin', type=int, default=0)
    ap.add_argument('--cycles', type=int, default=0)
    ap.add_argument('--port', type=int, default=44200)
    ap.add_argument('--label', default='')
    ap.add_argument('--timeout-s', type=float, default=3.0, help=argparse.SUPPRESS)
    args = ap.parse_args()
    # The lobby rules alone first (group_lobby.c): a failure there makes the console runs below meaningless.
    for name in ('lobby_unit', 'auto_handicap_unit'):
        exe = TOOLS / (name + EXE)
        if not exe.exists() and name == 'auto_handicap_unit':
            print('auto_handicap_unit: not built (no decomp source here)', flush=True)
            continue
        unit = subprocess.run([str(exe)], capture_output=True, text=True)
        print(unit.stdout.strip() or unit.stderr.strip(), flush=True)
        if unit.returncode != 0:
            print(f'RESULT: FAIL ({name})')
            return 1
    if args.wireless_off:
        return wireless_off(args)
    if args.lost or args.last_leaves:
        args.players, args.games, args.via_menu = 2, 1, True
    if args.cycles:
        args.players, args.games = 3, args.cycles + 1
    rejoin_who, rejoin_frame = (int(x) for x in args.rejoin.split(':')) if args.rejoin else (0, 0)
    if args.results_rejoin:
        args.results_leave = str(args.results_rejoin)
    n = args.players
    crash = {}
    if rejoin_who:
        crash[rejoin_who] = rejoin_frame
    if args.crash:
        who, frame = args.crash.split(':')
        crash[int(who)] = int(frame)
    def who_ms(opt):
        if not opt:
            return 0, 0
        a, b = opt.split(':') if ':' in opt else (opt, '')
        return int(a), b
    hold_who, hold_ms = who_ms(args.results_hold)
    linger_who, linger_ms = who_ms(args.results_linger)
    leave_who, leave_how = who_ms(args.results_leave)
    out = OUT / ('group' + (('-' + args.label) if args.label else ''))
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    procs = {}
    try:
        for i in range(n):
            folder = out / f'p{i + 1}'
            folder.mkdir()
            late = args.late == i + 1
            lines = [f'role={"host" if i == 0 else "join"}', 'udp=1', f'port={args.port}', f'delay={args.delay}',
                     f'test_players={n - (1 if args.late else 0)}', f'test_games={args.games - (1 if late else 0)}',
                     f'test_frames={args.frames}']
            if late:
                lines.append('test_late=1')
            if args.via_menu:
                lines += ['menu=1', 'via_menu=1']
            if args.drop:
                lines.append(f'drop_pct={args.drop}')
            if i + 1 in crash:
                lines.append(f'test_crash_frame={crash[i + 1]}')
            if hold_who == i + 1:
                lines.append(f'test_results_hold_ms={hold_ms}')
            if linger_who == i + 1:
                lines.append(f'test_results_linger_ms={linger_ms}')
            if args.stale_view == i + 1:
                lines.append('test_stale_view=1')
            if leave_who == i + 1:
                lines.append(f'test_results_leave={2 if leave_how == "vanish" else 1}')
            if args.lost and i == 0:
                lines.append('test_linger_ms=1000' if args.lost == 'host-left' else 'test_vanish_ms=1000')
            if args.lost and i == 1:
                lines.append('test_after_lost=1')
            if args.last_leaves and i == 0:
                lines.append('test_linger_ms=3000')
            if i + 1 in (rejoin_who, args.results_rejoin) or (args.cycles and i == 1):
                lines.append(f'name=P{i + 1}')
            if args.cycles and i == 1:
                lines.append('test_results_leave=1')
            if i == 0 and (args.results_rejoin or args.cycles):
                lines.append('test_wait_each=1')   # wait for the newcomer before each next game
            (folder / 'group.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        if args.fifth:
            (out / 'p5').mkdir()
            (out / 'p5' / 'group.ini').write_text(f'role=join\nudp=1\nport={args.port}\ntest_full=1\n', encoding='utf-8')
        for i in range(n):
            log = open(out / f'player{i + 1}.log', 'w')
            procs[f'p{i + 1}'] = (subprocess.Popen([str(TOOLS / ('group_client' + EXE)), '--dir', str(out / f'p{i + 1}')],
                                                   stdout=log, stderr=subprocess.STDOUT), log)
            time.sleep(0.5)
        rejoiners = []   # (label, port, test_games, extra ini lines, log text that starts it, the console whose log)
        if rejoin_who:
            rejoiners.append((f'p{rejoin_who}r', rejoin_who, args.games - 1, ['test_late=1'], 'vanishing at frame', f'p{rejoin_who}'))
        if args.results_rejoin:
            w = args.results_rejoin
            rejoiners.append((f'p{w}r', w, args.games - 1, [], 'leaving on the results screen', f'p{w}'))
        for k in range(1, args.cycles + 1):
            last = k == args.cycles
            rejoiners.append((f'p2r{k}', 2, 1, [] if last else ['test_results_leave=1'], 'leaving on the results screen',
                              'p2' if k == 1 else f'p2r{k - 1}'))
        for label, port_, games_, extra, trigger, watch in rejoiners:
            folder = out / label
            folder.mkdir()
            lines = ['role=join', 'udp=1', f'port={args.port}', f'delay={args.delay}', f'test_games={games_}',
                     f'test_frames={args.frames}', f'name=P{port_}'] + extra
            if args.drop:
                lines.append(f'drop_pct={args.drop}')
            (folder / 'group.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        for label, port_, games_, extra, trigger, watch in rejoiners:
            log_path = out / f'player{watch[1:]}.log'
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline and trigger not in log_path.read_text(errors='replace'):
                time.sleep(0.1)
            if trigger == 'vanishing at frame':
                time.sleep(args.timeout_s)   # the host has made it a CPU and freed the port
            log = open(out / f'player{label[1:]}.log', 'w')
            procs[label] = (subprocess.Popen([str(TOOLS / ('group_client' + EXE)), '--dir', str(out / label)],
                                             stdout=log, stderr=subprocess.STDOUT), log)
        if args.fifth:
            time.sleep(2)
            log = open(out / 'player5.log', 'w')
            procs['p5'] = (subprocess.Popen([str(TOOLS / ('group_client' + EXE)), '--dir', str(out / 'p5')],
                                            stdout=log, stderr=subprocess.STDOUT), log)
        end = time.monotonic() + 240
        codes = {}
        for name, (p, log) in procs.items():
            try:
                codes[name] = p.wait(max(1, end - time.monotonic()))
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
                codes[name] = 'timeout'
    finally:
        for p, log in procs.values():
            if p.poll() is None:
                p.kill()
                p.wait()
            log.close()
    print('exit codes:', codes)
    hashes = {}
    for i in range(n):
        path = out / f'player{i + 1}.log'
        print(f'---- {path.name}')
        print(tail(path, 'group-test:', 5))
        found = [l.split('bundle hash')[1].split(',')[0].strip() for l in path.read_text(errors='replace').splitlines() if 'bundle hash' in l]
        hashes[i + 1] = ' '.join(found)
    for label, *_ in rejoiners:
        path = out / f'player{label[1:]}.log'
        print(f'---- {path.name}')
        print(tail(path, 'group-test:', 5))
        found = [l.split('bundle hash')[1].split(',')[0].strip() for l in path.read_text(errors='replace').splitlines() if 'bundle hash' in l]
        hashes[label] = ' '.join(found)
    survivors = [i + 1 for i in range(n) if i + 1 not in crash]
    ok = all(codes[f'p{i}'] == 0 for i in survivors) and all(codes[label] == 0 for label, *_ in rejoiners)
    full = [i for i in survivors if i != args.late and i != leave_who and not (args.cycles and i == 2)]

    def log_of(who):
        return (out / f'player{str(who).lstrip("p")}.log').read_text(errors='replace')

    def times(who, what):
        text = (out / f'player{who}.log').read_text(errors='replace')
        return [int(l.split(what + ' at ')[1].split()[0]) for l in text.splitlines() if what + ' at ' in l]
    if hold_who:
        pressed = times(hold_who, 'ready on the results screen')
        ended = [t for i in full for t in times(i, 'the results are over')[:1]]
        early = [t for t in ended if not pressed or t < pressed[0]]
        print(f'results hold: console {hold_who} ready at {pressed[:1]}, results over at {sorted(ended)}')
        ok = ok and bool(pressed) and len(ended) == len(full) and not early
    if linger_who:
        back = times(linger_who, 'back in the lobby')
        block2 = times(1, 'match block 2')
        refused = 'Start refused while consoles' in (out / 'player1.log').read_text(errors='replace')
        print(f'results linger: console {linger_who} back at {back[:1]}, game 2 block at {block2}, host Start refused: {refused}')
        ok = ok and bool(back) and bool(block2) and block2[0] >= back[0] and refused
    if args.stale_view:
        text = (out / f'player{args.stale_view}.log').read_text(errors='replace')
        stale = [l.split('group-test: ')[1] for l in text.splitlines() if 'stale view at the results' in l]
        let = times(args.stale_view, 'the lobby view is let through again')
        ready = times(args.stale_view, 'ready on the results screen')
        over = times(args.stale_view, 'the results are over')
        print(f'stale view: {stale[:1]}, let through at {let[:1]}, ready at {ready[:1]}, over at {over[:1]}')
        ok = ok and bool(stale) and 'phase 0, after the match 0' in stale[0] and bool(let) and bool(ready) \
            and bool(over) and over[0] >= let[0] and ready[0] >= let[0]
    if args.lost:
        want = 'The host left' if args.lost == 'host-left' else 'The host stopped responding'
        note = [l.split('lost note: ')[1] for l in log_of(2).splitlines() if 'lost note: ' in l]
        page = [l.split('page after the loss: ')[1] for l in log_of(2).splitlines() if 'page after the loss: ' in l]
        print(f'lost: the joiner\'s Host/Join page shows {note}, page {page}')
        text = log_of(2)
        off, on, note_at = text.find('HOME and sleep off'), text.find('HOME and sleep on'), text.find('lost note: ')
        print(f'lost: HOME and sleep off at {off}, on at {on}, the note at {note_at} (log offsets)')
        # the Host/Join page browses again at once, so HOME and sleep stay off: never on between the close and the reopen
        ok = ok and note == [want] and page == ['2'] and 0 <= off < note_at and on < 0
    if args.last_leaves:
        host = log_of(1)
        bad = [l for l in host.splitlines() if 'lost note' in l or 'group: The ' in l or 'was lost' in l]
        print(f'last leaves: the host\'s lost lines {bad}')
        ok = ok and not bad
    for label, port_, games_, extra, trigger, watch in rejoiners:
        text = log_of(label)
        port_line = [l for l in text.splitlines() if 'group-test: in the lobby on port' in l]
        got = port_line[0].split('on port ')[1].strip() if port_line else '?'
        print(f'{label}: got port {got}, bundle hashes {hashes[label]}')
        ok = ok and got == str(port_)
        if 'test_late=1' in extra:
            ok = ok and 'watched the match in progress' in text and 'held through the results' in text
        if args.cycles:
            k = int(label[3:])
            ok = ok and hashes[label].split() == hashes[full[0]].split()[k:k + 1]
            doors = [l for l in text.splitlines() if 'group-test: lobby present' in l]
            print(f'{label}: first lobby seen: {doors[:1]}')
            # port 2's door when the newcomer arrives: the host's (a CPU or closed after the last one left), never Human
            first = doors[0].split(' doors ')[1].split() if doors else []
            ok = ok and bool(first) and not first[1].startswith('1:0/')
        else:
            ok = ok and hashes[label].split() == hashes[full[0]].split()[1:]
    if args.cycles:
        ok = ok and hashes[2].split() == hashes[full[0]].split()[:1]
    if leave_who and not args.cycles:
        left = times(leave_who, 'leaving on the results screen')
        print(f'results leave: console {leave_who} left at {left}')
        ok = ok and bool(left) and hashes[leave_who].split() == hashes[full[0]].split()[:1]
    ok = ok and len({hashes[i] for i in full}) == 1 and hashes[full[0]] != ''
    ok = ok and len(hashes[full[0]].split()) == args.games
    if args.late:
        late_log = (out / f'player{args.late}.log').read_text(errors='replace')
        watched = [l for l in late_log.splitlines() if 'watched the match in progress' in l]
        print('late console:', watched[-1].split('group-test: ')[-1] if watched else 'never watched')
        held = [l for l in late_log.splitlines() if 'held through the results' in l]
        print('late console:', held[-1].split('group-test: ')[-1] if held else 'not held through the results')
        ok = ok and bool(watched) and bool(held) and hashes[args.late].split() == hashes[full[0]].split()[1:]
    if args.fifth:
        print('fifth console:', tail(out / 'player5.log', 'group-test:', 3).replace('\n', ' | '))
        ok = ok and codes.get('p5') == 0
    print('bundle hashes:', hashes)
    print('RESULT:', 'PASS' if ok else 'FAIL', '->', out)
    return 0 if ok else 1


def wireless_off(args):
    out = OUT / ('group' + (('-' + args.label) if args.label else '') + '-wireless-off')
    if out.exists():
        shutil.rmtree(out)
    (out / 'p1').mkdir(parents=True)
    (out / 'p1' / 'group.ini').write_text(f'role=join\nudp=1\nport={args.port}\nmenu=1\nvia_menu=1\n'
                                          'test_wireless_off=1\ntest_expect_error=1\n', encoding='utf-8')
    with open(out / 'player1.log', 'w') as log:
        code = subprocess.run([str(TOOLS / ('group_client' + EXE)), '--dir', str(out / 'p1')], stdout=log,
                              stderr=subprocess.STDOUT, timeout=60).returncode
    text = (out / 'player1.log').read_text(errors='replace')
    errors = [l.split('menu error: ')[1] for l in text.splitlines() if 'menu error: ' in l]
    print(f'wireless off: exit {code}, menu errors {errors}')
    ok = code == 0 and errors == ['Please enable wireless and try again'] * 2
    print('RESULT:', 'PASS' if ok else 'FAIL', '->', out)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
