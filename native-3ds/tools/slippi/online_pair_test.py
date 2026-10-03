"""Two emulated 3DS consoles play Slippi Direct against each other through the
local fake matchmaking server: deterministic test inputs on both sides, the
first console ends each game with L+R+A+Start, A presses get both through the
results screen, and a second game follows. Each console records the match
(record=1); the records of the two sides must be identical.

    python tools/slippi/online_pair_test.py [--games 2] [--end-frame 900] [--delay 2]
Never contacts mm.slippi.gg.
"""
import argparse
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402
from net_test import OUT, TOOLS, USERS, start, write_profile  # noqa: E402

ROOT = emu.ROOT
KEY_A = 1
KEY_START = 8


def symbols():
    nm = ROOT / '.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'
    out = subprocess.check_output([str(nm), str(ROOT / 'build/game-opt/melee.elf')], text=True)
    return {p[2]: int(p[0], 16) for line in out.splitlines() if len(p := line.split()) == 3}


def poke(port, address, value):
    try:
        with socket.create_connection(('127.0.0.1', port), 5) as s:
            s.settimeout(10)
            packet(s, '?')
            receive(s)
            data = struct.pack('<I', value)
            packet(s, f'M{address:x},4:{data.hex()}')
            receive(s)
            packet(s, 'c')
    except OSError as e:
        print('gdb poke failed', port, e)


def records(path):
    data = Path(path).read_bytes()
    out = {}
    for off in range(0, len(data) - 39, 40):
        rec = data[off:off + 40]
        if rec[0:1] in (b'P', b'O', b'I'):
            frame = struct.unpack('>i', rec[4:8])[0]
            out[(rec[0:1], frame, rec[1])] = rec
    return out


def compare(a, b):
    ra, rb = records(a), records(b)
    keys = sorted(set(ra) & set(rb), key=lambda k: (k[1], k[0], k[2]))
    diff = [k for k in keys if ra[k] != rb[k]]
    only = len(set(ra) ^ set(rb))
    frames = max((k[1] for k in keys), default=0)
    return len(keys), diff, only, frames


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--games', type=int, default=2)
    ap.add_argument('--end-frame', type=int, default=900)
    ap.add_argument('--delay', type=int, default=2)
    ap.add_argument('--seconds', type=float, default=600)
    ap.add_argument('--mm-port', type=int, default=43113)
    ap.add_argument('--code-encoding', default='fullwidth')
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--mm-hold-first', type=int, default=0, help='ms the fake MM holds the first ticket match reply')
    args = ap.parse_args()

    out = OUT / 'pair'
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    sym = symbols()
    keys = sym['mp_test_keys']
    dsx = (ROOT / 'dist/3ds/melee/melee-development.3dsx').resolve()
    mm_cmd = [TOOLS / 'slippi_fake_mm.exe', '--port', args.mm_port]
    if args.mm_hold_first:
        mm_cmd += ['--delay-first-ms', args.mm_hold_first]
    mm = start(mm_cmd, out / 'fake_mm.log')
    time.sleep(0.5)
    sides = []
    for name, who, opp, extra, port in (
            ('a', 'a', 'PEER#002', ['character=2', 'stage=32', f'test_inputs=1', f'test_end_frame={args.end_frame}'], 24850),
            ('b', 'b', 'TEST#001', ['character=20', 'stage=31', 'test_inputs=2'], 24851)):
        # Two Vulkan instances can lose the GPU device; OpenGL is stable here.
        home = emu.prepare('pair-' + name, port, graphics='opengl')
        sd = emu.sd(home)
        write_profile(sd / 'slippi', who, opp, args, args.delay,
                      ['color=0', 'stage_select=1', 'boot_menu=0', 'record=1', *extra])
        for f in ('game.log', 'slippi/online-out.bin'):
            (sd / f).unlink(missing_ok=True)
        sides.append(dict(name=name, home=home, sd=sd, port=port, proc=None, exits=0, starts=0, copied=0))
    for s in sides:
        s['proc'] = emu.launch(s['home'], dsx, hidden=not args.show)
        time.sleep(3)
    deadline = time.monotonic() + args.seconds
    press = 0
    try:
        while time.monotonic() < deadline:
            time.sleep(1)
            for s in sides:
                log = s['sd'] / 'game.log'
                text = log.read_text(errors='replace') if log.exists() else ''
                s['starts'] = text.count('Slippi online: match starts')
                s['exits'] = text.count('Slippi online: match exit')
                if s['exits'] > s['copied'] and (s['sd'] / 'slippi/online-out.bin').exists():
                    time.sleep(1)
                    shutil.copy2(s['sd'] / 'slippi/online-out.bin', out / f"{s['name']}-game{s['exits']}.bin")
                    s['copied'] = s['exits']
                if s['proc'].poll() is not None:
                    raise SystemExit(f"emulator {s['name']} exited")
            if all(s['exits'] >= args.games for s in sides):
                break
            # Through the results screen: tap START+A while a side has ended a game
            # and the next has not started.
            press ^= 1
            for s in sides:
                if s['exits'] >= s['starts'] and s['exits'] > 0:
                    poke(s['port'], keys, (KEY_START | KEY_A) if press else 0)
    finally:
        for s in sides:
            emu.stop(s['proc'])
            if (s['sd'] / 'game.log').exists():
                shutil.copy2(s['sd'] / 'game.log', out / f"{s['name']}-game.log")
        p, f = mm
        p.kill()
        f.close()
    ok = True
    for g in range(1, args.games + 1):
        a, b = out / f'a-game{g}.bin', out / f'b-game{g}.bin'
        if not (a.exists() and b.exists()):
            print(f'game {g}: missing record (a {a.exists()}, b {b.exists()})')
            ok = False
            continue
        n, diff, only, frames = compare(a, b)
        print(f'game {g}: {n} shared records to frame {frames}, {len(diff)} differ, {only} on one side only')
        if diff:
            ok = False
            print('  first difference:', diff[0])
    for s in sides:
        lines = (out / f"{s['name']}-game.log").read_text(errors='replace').splitlines()
        print(f"---- {s['name']}")
        print('\n'.join(l for l in lines if l.startswith('Slippi online') and 'still waiting' not in l)[-3000:])
        print('\n'.join([l for l in lines if l.startswith('Rates:')][-2:]))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
