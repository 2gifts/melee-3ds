"""Play a Slippi replay in the port (Azahar) and compare every frame.

    python tools/slippi/replay_run.py REPLAY.slp [--instance replay] [--dsx PATH]
                                      [--timeout SECONDS] [--keep]

Needs a development build (dist/3ds/melee/melee-development.3dsx). Results go to
build/slippi-replays/<replay name>/ (replay-out.bin, game.log, compare.txt).
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
from replay_pack import pack  # noqa: E402
from slp import parse  # noqa: E402

ROOT = emu.ROOT


def ended(path):
    try:
        data = path.read_bytes()
    except OSError:
        return False
    return len(data) >= 40 and len(data) % 40 == 0 and data[-40:-39] == b'E'


def run(slp, instance='replay', dsx=None, timeout=None, port=24820, keep=False):
    slp = Path(slp)
    replay = parse(slp)
    frames = replay.last - replay.first + 1
    timeout = timeout or 120 + frames / 60 * 6
    home = emu.prepare(instance, port)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi/replay.bin').write_bytes(pack(replay))
    out = sd / 'slippi/replay-out.bin'
    out.unlink(missing_ok=True)
    (sd / 'game.log').unlink(missing_ok=True)
    dsx = Path(dsx or ROOT / 'dist/3ds/melee/melee-development.3dsx')
    result = ROOT / 'build/slippi-replays' / slp.stem
    result.mkdir(parents=True, exist_ok=True)
    proc = emu.launch(home, dsx.resolve())
    start = time.monotonic()
    status = 'timeout'
    try:
        while time.monotonic() - start < timeout:
            if ended(out):
                status = 'ended'
                break
            if proc.poll() is not None:
                status = f'emulator exited {proc.returncode}'
                break
            time.sleep(1)
    finally:
        emu.stop(proc)
        if not keep:
            (sd / 'slippi/replay.bin').unlink(missing_ok=True)
    for name in ('game.log',):
        if (sd / name).exists():
            shutil.copy2(sd / name, result / name)
    if out.exists():
        shutil.copy2(out, result / 'replay-out.bin')
    elapsed = time.monotonic() - start
    print(f'{slp.name}: {status} after {elapsed:.0f} s')
    if not (result / 'replay-out.bin').exists():
        print('no output; see', result / 'game.log')
        return 2
    cmp = subprocess.run([sys.executable, str(HERE / 'replay_compare.py'), str(slp), str(result / 'replay-out.bin')],
                         capture_output=True, text=True)
    (result / 'compare.txt').write_text(cmp.stdout + cmp.stderr)
    print(cmp.stdout + cmp.stderr)
    return cmp.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('slp')
    ap.add_argument('--instance', default='replay')
    ap.add_argument('--port', type=int, default=24820)
    ap.add_argument('--dsx')
    ap.add_argument('--timeout', type=float)
    ap.add_argument('--keep', action='store_true')
    a = ap.parse_args()
    sys.exit(run(a.slp, a.instance, a.dsx, a.timeout, a.port, a.keep))


if __name__ == '__main__':
    main()
