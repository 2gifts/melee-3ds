"""2 to 4 emulated 3DS consoles play a local wireless match (the development build's loopback UDP link,
local_udp=1: Azahar cannot join a UDS room headless). Deterministic test inputs on every console, the host ends
the match with L+R+A+Start, every console records the match (record=1) and the records must be identical.
--kill N:SECONDS stops console N (2..4) that long after every console has started its match: the others must
carry on, turn that player into a level 3 CPU at the same frame, and still produce identical records.

    docker/emu-test.sh run tools/slippi/online_quad_test.py [--players 4] [--end-frame 600] [--kill 3:20]
Four emulated consoles are CPU bound: this checks determinism, not timing.
"""
import argparse
import os
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from net_test import OUT  # noqa: E402
from online_pair_test import compare  # noqa: E402

ROOT = emu.ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--players', type=int, default=4)
    ap.add_argument('--end-frame', type=int, default=600)
    ap.add_argument('--delay', type=int, default=3)
    ap.add_argument('--seconds', type=float, default=1500)
    ap.add_argument('--kill', default='', help='N:SECONDS, stop console N that long after all matches started')
    ap.add_argument('--timeout-ms', type=int, default=60000, help='local_timeout_ms (the real default is 2000; emulation stalls longer)')
    ap.add_argument('--port', type=int, default=47010)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--graphics', default=None)
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    n = args.players
    kill = None
    if args.kill:
        who, secs = args.kill.split(':')
        kill = (int(who), float(secs))

    out = OUT / 'quad'
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    dsx = Path(args.dsx).resolve()
    sides = []
    for i in range(n):
        home = emu.prepare(f'quad-{i + 1}', 24850 + i, graphics=args.graphics)
        sd = emu.sd(home)
        (sd / 'slippi').mkdir(parents=True, exist_ok=True)
        lines = [f'opponent={"@host" if i == 0 else "@join"}', 'local_udp=1', f'local_players={n}',
                 f'local_port={args.port}', f'delay={args.delay}', 'character=2', 'color=0', 'stage=32', 'stage_select=1',
                 'auto_match=1', 'record=1', f'local_timeout_ms={args.timeout_ms}', f'test_inputs={i + 1}']
        if i == 0:
            lines.append(f'test_end_frame={args.end_frame}')
        (sd / 'slippi' / 'config.ini').write_text('\n'.join(lines) + '\n')
        for f in ('game.log', 'slippi/online-out.bin'):
            (sd / f).unlink(missing_ok=True)
        sides.append(dict(name=f'p{i + 1}', home=home, sd=sd, proc=None, killed=False, copied=False, started=False))
    for s in sides:
        s['proc'] = emu.launch(s['home'], dsx, hidden=not args.show)
        time.sleep(3)
    deadline = time.monotonic() + args.seconds
    all_started_at = None
    try:
        while time.monotonic() < deadline:
            time.sleep(1)
            for i, s in enumerate(sides):
                log = s['sd'] / 'game.log'
                text = log.read_text(errors='replace') if log.exists() else ''
                s['started'] = 'Slippi online: match starts' in text
                if 'Slippi online: match exit' in text and not s['copied'] and (s['sd'] / 'slippi/online-out.bin').exists():
                    time.sleep(1)
                    shutil.copy2(s['sd'] / 'slippi/online-out.bin', out / f"{s['name']}.bin")
                    s['copied'] = True
                if s['proc'].poll() is not None and not s['killed']:
                    raise SystemExit(f"emulator {s['name']} exited")
            if all(s['started'] for s in sides) and all_started_at is None:
                all_started_at = time.monotonic()
                print('all consoles started their match')
            if kill and all_started_at and not sides[kill[0] - 1]['killed'] and time.monotonic() - all_started_at >= kill[1]:
                s = sides[kill[0] - 1]
                print(f"stopping console {s['name']}")
                emu.stop(s['proc'])
                s['killed'] = True
            if all(s['copied'] or s['killed'] for s in sides):
                break
    finally:
        for s in sides:
            if not s['killed']:
                emu.stop(s['proc'])
            if (s['sd'] / 'game.log').exists():
                shutil.copy2(s['sd'] / 'game.log', out / f"{s['name']}-game.log")
    alive = [s for s in sides if not s['killed']]
    ok = all(s['copied'] for s in alive)
    if not ok:
        print('missing records:', [s['name'] for s in alive if not s['copied']])
    base = alive[0]
    for s in alive[1:]:
        a, b = out / f"{base['name']}.bin", out / f"{s['name']}.bin"
        if not (a.exists() and b.exists()):
            continue
        cnt, diff, only, frames = compare(a, b)
        print(f"{base['name']} vs {s['name']}: {cnt} shared records to frame {frames}, {len(diff)} differ, {only} on one side only")
        if diff or not cnt:
            ok = False
            print('  first difference:', diff[0] if diff else None)
    for s in sides:
        path = out / f"{s['name']}-game.log"
        if path.exists():
            lines = path.read_text(errors='replace').splitlines()
            print(f"---- {s['name']}")
            print('\n'.join(l for l in lines if (l.startswith('Slippi online') and 'still waiting' not in l) or 'star:' in l)[-1800:])
    print('RESULT:', 'PASS' if ok else 'FAIL', '->', out)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
