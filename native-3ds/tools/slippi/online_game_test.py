"""Full-game online test in Azahar against the local fake Slippi peer.

The dev build boots into Slippi online mode (config.ini names the opponent),
matchmakes through the fake server on 127.0.0.1, and plays a match whose
remote player follows the fake peer's test-pattern pads. Never contacts
mm.slippi.gg.

    python tools/slippi/online_game_test.py [--frames 3600] [--seconds 150]
        [--delay 3] [--peer-delay 2] [--peer-host] [--stage 32] [--character 2]
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import emu  # noqa: E402
from net_test import OUT, TOOLS, USERS, start, write_profile  # noqa: E402

ROOT = emu.ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=str(ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--frames', type=int, default=3600)
    ap.add_argument('--seconds', type=float, default=150)
    ap.add_argument('--delay', type=int, default=3)
    ap.add_argument('--peer-delay', type=int, default=2)
    ap.add_argument('--peer-host', action='store_true', help='fake peer is the decider (3DS is port 2)')
    ap.add_argument('--stage', type=int, default=32)
    ap.add_argument('--character', type=int, default=2)
    ap.add_argument('--mm-port', type=int, default=43113)
    ap.add_argument('--code-encoding', default='fullwidth')
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--peer-wait', type=float, default=2, help='seconds before the fake PC peer starts (its player picking)')
    args = ap.parse_args()

    out = OUT / 'game'
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    procs = {}
    mm_cmd = [TOOLS / 'slippi_fake_mm.exe', '--port', args.mm_port] + ([] if args.peer_host else ['--second-is-host'])
    procs['mm'] = start(mm_cmd, out / 'fake_mm.log')
    time.sleep(0.5)
    write_profile(out / 'b', 'b', USERS['a']['connectCode'], args, args.peer_delay)
    home = emu.prepare('game', 24840)
    sd = emu.sd(home)
    write_profile(sd / 'slippi', 'a', USERS['b']['connectCode'], args, args.delay,
                  [f'character={args.character}', 'color=0', f'stage={args.stage}', 'stage_select=1', 'boot_menu=0'])
    log = sd / 'game.log'
    log.unlink(missing_ok=True)
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    time.sleep(args.peer_wait)   # the 3DS tickets first (the fake MM gives isHost to the first ticket)
    procs['b'] = start([TOOLS / 'slippi_fake_peer.exe', '--dir', out / 'b', '--frames', args.frames,
                        '--linger', 5], out / 'peer_b.log')
    deadline = time.monotonic() + args.seconds
    try:
        while time.monotonic() < deadline and proc.poll() is None:
            time.sleep(1)
            if procs['b'][0].poll() is not None:
                time.sleep(3)
                break
    finally:
        emu.stop(proc)
        for name, (p, f) in procs.items():
            if p.poll() is None:
                p.kill()
                p.wait()
            f.close()
    if log.exists():
        shutil.copy(log, out / 'game.log')
        lines = log.read_text(errors='replace').splitlines()
        print('---- game.log (Slippi lines)')
        print('\n'.join(l for l in lines if 'Slippi' in l or '[slippi]' in l or 'PANIC' in l)[-6000:])
        rates = [l for l in lines if l.startswith('Rates:')]
        print('\n'.join(rates[-3:]))
    print('---- peer_b.log')
    print('\n'.join((out / 'peer_b.log').read_text(errors='replace').splitlines()[-15:]))


if __name__ == '__main__':
    main()
