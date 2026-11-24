"""Group play on PC consoles (group_client, the 3DS's group code over the loopback link): a host and 1..3 joiners meet in
a lobby, fill their doors (the host adds a CPU on a free port), start, pick a stage and play matches with test pads.
Every console that stays must report the same bundle hash for every game.

    python tools/group/group_test.py [--players 3] [--games 2] [--drop 30] [--via-menu] [--crash 3:200]
--via-menu   meet through the Melee -> Group pages (group_menu.c) instead of joining directly
--crash N:F  console N (2..4) vanishes without a goodbye at frame F of game 1: the others make it a CPU at the same
             frame and carry on (use --games 1: the game afterwards would wait for a console that is gone)
--fifth      after 4 consoles are in the lobby, a fifth tries to join: it must see the game as full and be refused
--late N     console N (2..4) joins only once game 1 is running: it must watch that match's status (the Match in
             progress page's data), not play it, and play games 2.. with everyone (needs --games 2 or more)

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
    ap.add_argument('--port', type=int, default=44200)
    ap.add_argument('--label', default='')
    args = ap.parse_args()
    n = args.players
    crash = {}
    if args.crash:
        who, frame = args.crash.split(':')
        crash[int(who)] = int(frame)
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
            (folder / 'group.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        if args.fifth:
            (out / 'p5').mkdir()
            (out / 'p5' / 'group.ini').write_text(f'role=join\nudp=1\nport={args.port}\ntest_full=1\n', encoding='utf-8')
        for i in range(n):
            log = open(out / f'player{i + 1}.log', 'w')
            procs[f'p{i + 1}'] = (subprocess.Popen([str(TOOLS / ('group_client' + EXE)), '--dir', str(out / f'p{i + 1}')],
                                                   stdout=log, stderr=subprocess.STDOUT), log)
            time.sleep(0.5)
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
    survivors = [i + 1 for i in range(n) if i + 1 not in crash]
    ok = all(codes[f'p{i}'] == 0 for i in survivors)
    full = [i for i in survivors if i != args.late]
    ok = ok and len({hashes[i] for i in full}) == 1 and hashes[full[0]] != ''
    ok = ok and len(hashes[full[0]].split()) == args.games
    if args.late:
        late_log = (out / f'player{args.late}.log').read_text(errors='replace')
        watched = [l for l in late_log.splitlines() if 'watched the match in progress' in l]
        print('late console:', watched[-1].split('group-test: ')[-1] if watched else 'never watched')
        ok = ok and bool(watched) and hashes[args.late].split() == hashes[full[0]].split()[1:]
    if args.fifth:
        print('fifth console:', tail(out / 'player5.log', 'group-test:', 3).replace('\n', ' | '))
        ok = ok and codes.get('p5') == 0
    print('bundle hashes:', hashes)
    print('RESULT:', 'PASS' if ok else 'FAIL', '->', out)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
