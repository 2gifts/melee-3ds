"""Drive the Slippi Direct menu flow in Azahar and capture both screens.

Boots the development build in a private emulator instance (GDB port 24689,
which the menu helpers in tools/ expect), then:
- reaches the main menu and opens 1P -> Online Play;
- picks a fighter on the online CSS and presses START;
- types the fake PC's code on the touch keyboard and confirms;
- starts the local fake matchmaking server and peer;
- captures each step.
Never contacts mm.slippi.gg.

    python tools/slippi/ui/ui_test.py [--steps css,keyboard,match] [--show]
Screens: build/slippi-ui/<step>-top.png / -bottom.png
"""
import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parents[1]
ROOT = TOOLS.parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(TOOLS))
os.environ.setdefault('MP_TEST_ELF', str(ROOT / 'build/game-opt/melee.elf'))
import emu  # noqa: E402
from net_test import USERS, start, write_profile  # noqa: E402

OUT = ROOT / 'build/slippi-ui'
PORT = 24689
KEYS = "1234567890QWERTYUIOPASDFGHJKL#ZXCVBNM"


def key_center(ch):
    i = KEYS.index(ch)
    col, row = (i % 10, i // 10)
    return 5 + col * 31 + 14, 76 + row * 27 + 12


OK_KEY = (239 + 37, 185 + 12)


def png(path, w, h, rows):
    import zlib

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\x00' + r for r in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def save_top(src, out):
    data = src.read_bytes()
    rows = []
    for y in range(240):
        row = bytearray()
        for x in range(400):
            i = (x * 240 + (239 - y)) * 3
            row += bytes((data[i + 2], data[i + 1], data[i]))
        rows.append(bytes(row))
    png(out, 400, 240, rows)


def save_bottom(src, out):
    data = src.read_bytes()
    rows = []
    for y in range(240):
        row = bytearray()
        for x in range(320):
            v = data[(x * 240 + (239 - y)) * 2] | data[(x * 240 + (239 - y)) * 2 + 1] << 8
            row += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        rows.append(bytes(row))
    png(out, 320, 240, rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=str(ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--icon', type=int, default=None, help='CSS icon for our fighter (default Fox)')
    ap.add_argument('--no-peer', action='store_true', help='only the menus, no fake opponent')
    ap.add_argument('--match-seconds', type=float, default=40)
    ap.add_argument('--mm-port', type=int, default=43113)
    ap.add_argument('--code-encoding', default='fullwidth')
    args = ap.parse_args()

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    procs = {}
    home = emu.prepare('ui', PORT)
    sd = emu.sd(home)
    write_profile(sd / 'slippi', 'a', USERS['b']['connectCode'], args, 2, ['prefetch=0'])
    (sd / 'slippi' / 'direct-codes.txt').unlink(missing_ok=True)
    (sd / 'game.log').unlink(missing_ok=True)
    if not args.no_peer:
        procs['mm'] = start([TOOLS / '../build/slippi-tools/slippi_fake_mm.exe', '--port', args.mm_port],
                            OUT / 'fake_mm.log')
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    for _ in range(600):
        try:
            with socket.create_connection(('127.0.0.1', PORT), .2):
                break
        except OSError:
            time.sleep(.1)
    import select_test_stage as select
    import bottom_screen_test as bottom
    from gameplay_test import symbols, packet, receive
    from profile_switch import set_word

    def patient(fn, *a, **k):
        # The stub can stall for seconds while the emulator loads a scene.
        for attempt in range(20):
            try:
                return fn(*a, **k)
            except (TimeoutError, ConnectionError, OSError) as e:
                if proc.poll() is not None:
                    raise RuntimeError('the emulator exited')
                print('stub busy:', e, flush=True)
                if attempt == 1:
                    subprocess.run([sys.executable, str(HERE / 'gdb_regs.py')], timeout=60)
                time.sleep(2)
        return fn(*a, **k)

    def capture(name):
        subprocess.run([sys.executable, str(TOOLS / 'capture_game.py')], check=True, capture_output=True)
        save_top(sd / 'engine-top.bgr', OUT / f'{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', OUT / f'{name}-bottom.png')
        print('captured', name, flush=True)

    def wait_scene(scene, mode=None, timeout=60):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            s = patient(bottom.snapshot)
            if s['scene'] == scene and (mode is None or s['mode'] == mode):
                return s
            time.sleep(.25)
        raise TimeoutError(f'scene {scene} mode {mode}: last {s["scene"]}/{s["mode"]}')

    def touch(x, y, frames=8):
        patient(set_word, 'mp_test_bottom_touch', x | (y << 16))
        patient(select.act, frames=frames)

    try:
        set_word('mp_test_frame_limit', 0)
        set_word('mp_slippi_no_scripts', 1)
        select.observe((0, 1, 0, 0))
        for _ in range(70):
            state = select.observe()
            if 'menu' in state:
                break
            select.act(0x100 if bottom.snapshot()['scene'] == 42 else 0x1000, 2)
            select.act(frames=20)
        else:
            raise AssertionError('no main menu')
        select.act(frames=30)
        capture('menu-main')
        # Main menu: 1P (hovered first) -> 1P menu; down twice to Online Play.
        select.act(0x100, 2)
        select.act(frames=40)
        for _ in range(2):
            select.act(0x4, 2)   # D-pad down
            select.act(frames=12)
        capture('menu-1p')
        select.act(0x100, 2)
        wait_scene(8, 8)
        select.act(frames=60)
        capture('css-idle')
        state = select.observe()
        icon = args.icon if args.icon is not None else 2
        bottom.choose(state, 0, icon)
        select.act(frames=30)
        capture('css-selected')
        select.act(0x1000, 2)   # START: code entry
        select.act(frames=20)
        capture('keyboard')
        for ch in USERS['b']['connectCode']:
            touch(*key_center(ch))
        select.act(frames=10)
        capture('keyboard-typed')
        if not args.no_peer:
            peer_dir = OUT / 'b'
            write_profile(peer_dir, 'b', USERS['a']['connectCode'], args, 2)
            procs['b'] = start([TOOLS / '../build/slippi-tools/slippi_fake_peer.exe', '--dir', peer_dir,
                                '--frames', 900, '--linger', 5], OUT / 'peer_b.log')
        touch(*OK_KEY)
        patient(select.act, frames=30)
        patient(capture, 'searching')
        if not args.no_peer:
            s = wait_scene(2, 8, timeout=120)
            patient(select.act, frames=200)
            patient(capture, 'match')
            end = time.monotonic() + args.match_seconds
            while time.monotonic() < end and patient(bottom.snapshot)['scene'] != 8:
                time.sleep(1)
            patient(select.act, frames=60)
            patient(capture, 'after-match')
    finally:
        emu.stop(proc)
        for name, (p, f) in procs.items():
            if p.poll() is None:
                p.kill()
                p.wait()
            f.close()
        log = sd / 'game.log'
        if log.exists():
            shutil.copy(log, OUT / 'game.log')
            lines = log.read_text(errors='replace').splitlines()
            print('\n'.join(l for l in lines if 'Slippi' in l or '[slippi]' in l or 'PANIC' in l
                            or 'Render context' in l)[-5000:])


if __name__ == '__main__':
    main()
