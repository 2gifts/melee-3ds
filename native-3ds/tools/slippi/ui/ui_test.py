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
    ap.add_argument('--pick-kind', type=int, default=None, help='SSS: stage kind to pick (default: printed list, first)')
    ap.add_argument('--frozen', action='store_true', help='SSS: press Z (frozen Stadium) before picking')
    ap.add_argument('--vanilla-menus', action='store_true', help='without the Slippi menu files')
    ap.add_argument('--buttons', action='store_true', help='type the code with the buttons, not touch')
    ap.add_argument('--title-scan', action='store_true', help='capture the CSS title at frames 0-23')
    ap.add_argument('--games', type=int, default=1, help='2: the 3DS quits game 1 (LRAS), picks the stage, plays game 2')
    ap.add_argument('--mm-port', type=int, default=43113)
    ap.add_argument('--code-encoding', default='fullwidth')
    args = ap.parse_args()

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    procs = {}
    home = emu.prepare('ui', PORT)
    sd = emu.sd(home)
    extra = ['prefetch=0']
    write_profile(sd / 'slippi', 'a', USERS['b']['connectCode'], args, 2, extra)
    (sd / 'slippi' / 'direct-codes.txt').unlink(missing_ok=True)
    # Slippi's menu files, made from this disc (tools/slippi/slippi_files.py).
    files = sd / 'slippi' / 'files'
    if files.exists():
        shutil.rmtree(files)
    if not args.vanilla_menus:
        subprocess.run([sys.executable, str(HERE.parent / 'slippi_files.py'), str(emu.MASTER / 'user/sdmc/3ds/melee/files'),
                        str(files)], check=True, capture_output=True)
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
        if args.games > 1:
            set_word('mp_slippi_test_lose', 1, 'big')
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
        if not args.vanilla_menus:
            # Slippi's Online submenu, Direct hovered.
            select.act(frames=40)
            capture('menu-online')
            select.act(0x100, 2)
        wait_scene(8, 8)
        select.act(frames=60)
        capture('css-idle')
        if args.title_scan:
            from PIL import Image
            tiles = []
            for f in range(0, 24):
                patient(set_word, 'mp_slippi_title_frame', f, 'big')
                patient(select.act, frames=6)
                capture(f'title{f}')
                tiles.append(Image.open(OUT / f'title{f}-top.png').crop((40, 0, 140, 45)))
            sheet = Image.new('RGB', (600, 180))
            for i, tile in enumerate(tiles):
                sheet.paste(tile, ((i % 6) * 100, (i // 6) * 45))
            sheet.save(OUT / 'title-sheet.png')
            return
        state = select.observe()
        icon = args.icon if args.icon is not None else 2
        bottom.choose(state, 0, icon)
        select.act(frames=30)
        capture('css-selected')
        if icon == 15:
            touch(242 + 35, 137 + 9)       # Zelda icon: SHEIK -> ZELDA
            select.act(frames=20)
            capture('css-zelda')
        touch(126 + 36, 187 + 11)          # SETTINGS
        touch(276 + 13, 38 + 9 + 13)       # delay +
        capture('settings')
        touch(160, 205)                    # outside: back
        select.act(0x1000, 2)   # START: code entry
        select.act(frames=20)
        capture('keyboard')
        if args.buttons:
            # Physical controls: D-pad right x3, X takes the suggestion
            # (config.ini's opponent= seeds the history); START later.
            def press(key, frames=4):
                patient(set_word, 'mp_test_keys', key)
                patient(select.act, frames=frames)
                patient(set_word, 'mp_test_keys', 0)
                patient(select.act, frames=4)
            for _ in range(3):
                press(16)
            capture('keyboard-moved')
            press(1024)
        else:
            for ch in USERS['b']['connectCode']:
                touch(*key_center(ch))
        select.act(frames=10)
        capture('keyboard-typed')
        if not args.no_peer:
            peer_dir = OUT / 'b'
            # No checksums from the fake PC: it does not run the game, and a
            # desync now ends the match (as in Slippi).
            write_profile(peer_dir, 'b', USERS['a']['connectCode'], args, 2, ['send_checksum=0'])
            procs['b'] = start([TOOLS / '../build/slippi-tools/slippi_fake_peer.exe', '--dir', peer_dir,
                                '--frames', 900, '--linger', 5, '--chat', '0x88', '--hold-ms', 25000,
                                '--games', args.games, '--no-checksum'],
                               OUT / 'peer_b.log')
        if args.buttons:
            patient(set_word, 'mp_test_keys', 8)
            patient(select.act, frames=4)
            patient(set_word, 'mp_test_keys', 0)
        else:
            touch(*OK_KEY)
        patient(select.act, frames=30)
        patient(capture, 'searching')
        if not args.no_peer:
            # Connected, waiting on the opponent (it holds 25 s): chat.
            patient(select.act, frames=240)
            patient(capture, 'connected')
            patient(select.act, 0x8, 2)        # D-pad up: page Up
            patient(select.act, frames=10)
            patient(capture, 'chat-page')
            patient(select.act, 0x1, 2)        # D-pad left: "one more"
            patient(select.act, frames=20)
            patient(capture, 'chat-sent')
            touch(126 + 36, 187 + 11)          # CHAT pill
            patient(capture, 'chat-bottom')
            touch(160, 205)                    # outside the pills: back
            try:
                wait_scene(32, 8, timeout=120)     # the VS splash
                patient(select.act, frames=70)
                patient(capture, 'splash')
            except TimeoutError as e:
                print('no splash:', e)
            s = wait_scene(2, 8, timeout=120)
            patient(select.act, frames=200)
            patient(capture, 'match')
            end = time.monotonic() + args.match_seconds
            while time.monotonic() < end and patient(bottom.snapshot)['scene'] != 8:
                time.sleep(1)
            patient(select.act, frames=60)
            patient(capture, 'after-match')
            if args.games > 1:
                patient(select.act, 0x1000, 2)     # START: the loser picks the stage
                wait_scene(9, 8, timeout=60)
                patient(select.act, frames=90)
                patient(capture, 'sss')
                s = patient(select.observe)
                print('SSS icons:', [(i, ic.get('stage_kind'), ic.get('available')) for i, ic in enumerate(s['icons'])])
                want = next(i for i, ic in enumerate(s['icons']) if ic.get('stage_kind') == args.pick_kind)
                for _ in range(120):
                    if s['selection'] == want:
                        s = patient(select.act, frames=12)
                        if s['selection'] == want:
                            break
                        continue
                    tx, ty = s['icons'][want]['xy']
                    dx, dy = tx - s['cursor'][0], ty - s['cursor'][1]
                    x = max(-80, min(80, round(dx / .03)))
                    y = max(-80, min(80, round(dy / .03)))
                    patient(select.observe, (0, 2, x, y))
                    s = patient(select.act, frames=3)
                if args.frozen:
                    patient(select.act, 0x10, 2)   # Z: frozen Stadium
                    patient(select.act, frames=20)
                    patient(capture, 'sss-frozen')
                patient(select.act, 0x100, 4)      # A: pick it
                wait_scene(8, 8, timeout=60)
                patient(select.act, frames=60)
                patient(capture, 'stage-picked')
                wait_scene(2, 8, timeout=120)
                patient(select.act, frames=200)
                patient(capture, 'game2')
                end = time.monotonic() + args.match_seconds
                while time.monotonic() < end and patient(bottom.snapshot)['scene'] != 8:
                    time.sleep(1)
                patient(select.act, frames=60)
                patient(capture, 'after-game2')
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
