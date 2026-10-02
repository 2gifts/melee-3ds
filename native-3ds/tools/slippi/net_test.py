"""Local Slippi network tests. Never contacts mm.slippi.gg: every config
written here points mm_host at the fake server on 127.0.0.1, with dummy
credentials.

    python tools/slippi/net_test.py pc-pair      # fake MM + two Dolphin-like fake peers
    python tools/slippi/net_test.py pc-client    # fake MM + fake peer + PC build of the 3DS self-test
    python tools/slippi/net_test.py azahar DSX   # fake MM + fake peer + the 3DS self-test in Azahar
Options: --frames N, --drop PCT (fake peer drops that share of its pad
packets), --delay-a/--delay-b, --mm-port, --seconds (emulator run time),
--second-is-host, --code-encoding ascii.

Build the tools first: python tools/slippi/build_tools.py
Logs: build/slippi-tests/<mode>/*.log
"""
import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'build/slippi-tools'
OUT = ROOT / 'build/slippi-tests'

USERS = {
    'a': {'uid': 'local-test-uid-a', 'playKey': 'dummy-play-key-a', 'connectCode': 'TEST#001', 'displayName': 'Test 3DS',
          'latestVersion': '3.6.4'},
    'b': {'uid': 'local-test-uid-b', 'playKey': 'dummy-play-key-b', 'connectCode': 'PEER#002', 'displayName': 'Fake Dolphin',
          'latestVersion': '3.6.4'},
}


def write_profile(folder, who, opponent, args, delay, extra=()):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'user.json').write_text(json.dumps(USERS[who], indent=2), encoding='utf-8')
    lines = [
        '# written by tools/slippi/net_test.py: local fake server only',
        f'opponent={opponent}',
        f'delay={delay}',
        'mm_host=127.0.0.1',
        f'mm_port={args.mm_port}',
        f'code_encoding={args.code_encoding}',
        *extra,
    ]
    (folder / 'config.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def start(cmd, log):
    f = open(log, 'w', encoding='utf-8')
    return subprocess.Popen([str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT), f


def finish(procs, timeout):
    end = time.monotonic() + timeout
    codes = {}
    for name, (p, f) in procs.items():
        try:
            codes[name] = p.wait(max(1, end - time.monotonic()))
        except subprocess.TimeoutExpired:
            p.kill()
            codes[name] = 'timeout'
        f.close()
    return codes


def tail(path, pattern=None, n=12):
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    if pattern:
        lines = [l for l in lines if pattern in l]
    return '\n'.join(lines[-n:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=('pc-pair', 'pc-client', 'azahar'))
    ap.add_argument('dsx', nargs='?')
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--drop', type=int, default=0)
    ap.add_argument('--delay-a', type=int, default=2)
    ap.add_argument('--delay-b', type=int, default=2)
    ap.add_argument('--mm-port', type=int, default=43113)
    ap.add_argument('--seconds', type=float, default=90)
    ap.add_argument('--second-is-host', action='store_true')
    ap.add_argument('--code-encoding', default='fullwidth')
    ap.add_argument('--label', default='')
    args = ap.parse_args()

    out = OUT / (args.mode + (('-' + args.label) if args.label else ''))
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    mm_cmd = [TOOLS / 'slippi_fake_mm.exe', '--port', args.mm_port] + (['--second-is-host'] if args.second_is_host else [])
    procs = {'mm': start(mm_cmd, out / 'fake_mm.log')}
    time.sleep(0.5)
    drop = [f'test_drop_pct={args.drop}'] if args.drop else []
    write_profile(out / 'b', 'b', USERS['a']['connectCode'], args, args.delay_b, drop)
    peer_b = [TOOLS / 'slippi_fake_peer.exe', '--dir', out / 'b', '--frames', args.frames]
    try:
        if args.mode == 'pc-pair':
            write_profile(out / 'a', 'a', USERS['b']['connectCode'], args, args.delay_a)
            procs['a'] = start([TOOLS / 'slippi_fake_peer.exe', '--dir', out / 'a', '--frames', args.frames], out / 'peer_a.log')
            time.sleep(0.3)
            procs['b'] = start(peer_b, out / 'peer_b.log')
            codes = finish({k: v for k, v in procs.items() if k != 'mm'}, 120)
        elif args.mode == 'pc-client':
            write_profile(out / 'a', 'a', USERS['b']['connectCode'], args, args.delay_a,
                          ['selftest=1', f'selftest_frames={args.frames}'])
            procs['a'] = start([TOOLS / 'slippi_client.exe', '--dir', out / 'a'], out / 'client_a.log')
            time.sleep(0.3)
            procs['b'] = start(peer_b, out / 'peer_b.log')
            codes = finish({k: v for k, v in procs.items() if k != 'mm'}, 120)
        else:
            if not args.dsx:
                raise SystemExit('azahar mode needs the .3dsx path')
            sys.path.insert(0, str(ROOT / 'tools/slippi'))
            import emu
            home = emu.prepare('net', 24812)
            sd = emu.sd(home)
            write_profile(sd / 'slippi', 'a', USERS['b']['connectCode'], args, args.delay_a,
                          ['selftest=1', f'selftest_frames={args.frames}'])
            log = sd / 'game.log'
            if log.exists():
                log.unlink()
            procs['b'] = start(peer_b, out / 'peer_b.log')
            proc = emu.launch(home, Path(args.dsx).resolve())
            deadline = time.monotonic() + args.seconds
            verdict = None
            while time.monotonic() < deadline and proc.poll() is None:
                time.sleep(1)
                if log.exists():
                    text = log.read_text(errors='replace')
                    if 'selftest: PASS' in text or 'selftest: FAIL' in text:
                        verdict = text
                        time.sleep(3)
                        break
            emu.stop(proc)
            if log.exists():
                shutil.copy(log, out / 'game.log')
                azahar_log = home / 'user/log/azahar_log.txt'
                if azahar_log.exists():
                    shutil.copy(azahar_log, out / 'azahar_log.txt')
            codes = finish({'b': procs['b']}, 30)
    finally:
        for name, (p, f) in procs.items():
            if p.poll() is None:
                p.kill()
                p.wait()
            if not f.closed:
                f.close()
    print('exit codes:', codes)
    for path in sorted(out.glob('*.log')):
        print(f'---- {path.name}')
        if path.name == 'game.log':
            print(tail(path, '[slippi]', 40))
        else:
            print(tail(path, None, 14))
    ok = all(c == 0 for c in codes.values())
    if args.mode == 'azahar':
        ok = ok and (out / 'game.log').exists() and 'selftest: PASS' in (out / 'game.log').read_text(errors='replace')
    print('RESULT:', 'PASS' if ok else 'FAIL', '->', out)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
