"""Tests of the local wireless module alone (port/3ds/localwireless), no game: N lw_client processes over the
loopback UDP link. Every console that stays must see identical bundles (same hash).

    python tools/localwireless/lw_test.py --players 4 [--frames 600] [--drop 50] [--crash 3:200]
--crash N:FRAME makes console N (1 = host) vanish without a goodbye at that frame. Build first:
python tools/slippi/build_tools.py (it builds lw_client too).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ.get('MELEE_TOOLS_DIR') or ROOT / 'build/slippi-tools')
OUT = Path(os.environ.get('MELEE_TEST_OUT') or ROOT / 'build/slippi-tests') / 'lw'
EXE = '.exe' if sys.platform == 'win32' else ''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--players', type=int, default=4)
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--delay', type=int, default=2)
    ap.add_argument('--drop', type=int, default=0)
    ap.add_argument('--port', type=int, default=47020)
    ap.add_argument('--timeout-ms', type=int, default=0)
    ap.add_argument('--crash', default='')
    args = ap.parse_args()
    crash = {}
    if args.crash:
        who, frame = args.crash.split(':')
        crash[int(who)] = int(frame)
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    procs = {}
    try:
        for i in range(args.players):
            cmd = [TOOLS / ('lw_client' + EXE), '--host' if i == 0 else '--join', '--players', args.players, '--frames', args.frames,
                   '--delay', args.delay, '--port', args.port, '--drop', args.drop, '--timeout-ms', args.timeout_ms]
            if i + 1 in crash:
                cmd += ['--crash', crash[i + 1]]
            log = open(OUT / f'player{i + 1}.log', 'w')
            procs[i + 1] = (subprocess.Popen([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT), log)
            time.sleep(0.4)
        codes = {}
        end = time.monotonic() + 240
        for k, (p, log) in procs.items():
            try:
                codes[k] = p.wait(max(1, end - time.monotonic()))
            except subprocess.TimeoutExpired:
                p.kill()
                codes[k] = 'timeout'
            log.close()
    finally:
        for p, log in procs.values():
            if p.poll() is None:
                p.kill()
    hashes = {}
    for k in procs:
        text = (OUT / f'player{k}.log').read_text(errors='replace')
        for line in text.splitlines():
            if 'bundle hash' in line:
                hashes[k] = line.split('bundle hash')[1].split(',')[0].strip()
    survivors = [k for k in procs if k not in crash]
    ok = all(codes[k] == 0 for k in survivors) and len({hashes.get(k) for k in survivors}) == 1 and None not in {hashes.get(k) for k in survivors}
    print('exit codes:', codes, 'hashes:', hashes)
    print('RESULT:', 'PASS' if ok else 'FAIL', '->', OUT)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
