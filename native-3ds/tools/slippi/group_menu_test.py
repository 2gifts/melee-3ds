"""Melee -> Solo / Group in Azahar drives the real VS menu of the development build.

Boots with local_group=1 and the loopback link (local_udp=1), then: main menu -> VS -> Melee (the Solo/Group page),
Back (B), Melee again -> Group (the Group page), Host (the lobby CSS in the HANYU scene, with the connected-players strip),
B out of the CSS (back on the VS menu, the session closed), and finally Melee -> Solo (the vanilla CSS).
Every step saves both screens as PNGs and the log lines are checked.

    docker/emu-test.sh run tools/slippi/group_menu_test.py [--seconds 600]
Results: $MELEE_TEST_OUT/group-menu/ (the PNGs, game.log)."""
import argparse
import os
import shutil
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402

PORT = 24862
KEY_A, KEY_B, KEY_START, KEY_DOWN = 1, 2, 8, 0x80
CPAD_UP, CPAD_RIGHT = 1 << 30, 1 << 28
FIELDS = 'scene mode stock_mode teams seconds timer stage rule_stocks rule_minutes items stamina menu'.split()


def png(path, w, h, rows):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def save_top(src, out):
    d = Path(src).read_bytes()
    rows = []
    for y in range(240):
        r = bytearray()
        for x in range(400):
            i = (x * 240 + (239 - y)) * 3
            r += bytes((d[i + 2], d[i + 1], d[i]))
        rows.append(r)
    png(out, 400, 240, rows)


def save_bottom(src, out):
    d = Path(src).read_bytes()
    rows = []
    for y in range(240):
        r = bytearray()
        for x in range(320):
            v = d[(x * 240 + (239 - y)) * 2] | d[(x * 240 + (239 - y)) * 2 + 1] << 8
            r += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        rows.append(r)
    png(out, 320, 240, rows)


class Stub:
    def __init__(self, sym):
        self.sym = sym

    def _session(self, fn):
        last = None
        for _ in range(10):
            try:
                with socket.create_connection(('127.0.0.1', PORT), 5) as s:
                    s.settimeout(10)
                    packet(s, '?')
                    receive(s)
                    try:
                        return fn(s)
                    finally:
                        packet(s, 'c')
            except (OSError, TimeoutError) as e:
                last = e
                time.sleep(1)
        raise last

    def poke(self, name, value, endian='little'):
        def f(s):
            packet(s, f'M{self.sym[name]:x},4:' + value.to_bytes(4, endian).hex())
            receive(s)
        self._session(f)

    def peek(self, name, n):
        def f(s):
            packet(s, f'm{self.sym[name]:x},{n:x}')
            return bytes.fromhex(receive(s))
        return self._session(f)

    def state(self):
        w = struct.unpack('<36I', self.peek('mp_bottom_observed', 144))
        return dict(zip(FIELDS, w))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--seconds', type=float, default=900)
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--top-menu', action='store_true', help='the top-screen Solo/Group submenu (local_group_menu=1; its labels are still Slippi\'s Ranked / Unranked) instead of the bottom-screen touch page')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-menu'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('local_group=1\nlocal_udp=1\nlocal_port=44100\nprefetch=0\ndelay=2\n' + ('local_group_menu=1\n' if args.top_menu else ''))
    (sd / 'game.log').unlink(missing_ok=True)
    stub = Stub(emu.elf_symbols(args.elf))
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    t0 = time.monotonic()
    results = []

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    expected = []

    def expect_log(name, text):
        expected.append((name, text))

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)

    def pulse(keys, hold=1.2):
        stub.poke('mp_test_keys', keys)
        time.sleep(hold)
        stub.poke('mp_test_keys', 0)
        time.sleep(0.8)

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        time.sleep(1.5)

    def capture(name):
        stub.poke('mp_test_capture', 1)
        for _ in range(120):
            time.sleep(0.5)
            if int.from_bytes(stub.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(sd / 'engine-top.bgr', out / f'{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', out / f'{name}-bottom.png')
        print('captured', name, flush=True)

    def wait(pred, what, timeout=240, press=None, hold=1.2):
        end = time.monotonic() + timeout
        s = None
        while time.monotonic() < end and proc.poll() is None:
            try:
                s = stub.state()
            except Exception as e:  # noqa: BLE001
                print('stub busy:', e, flush=True)
                time.sleep(2)
                continue
            if pred(s):
                return s
            if press:
                pulse(press, hold)
            else:
                time.sleep(1)
        check(what, False, f'last state {s}')
        raise SystemExit(1)

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        # Past the title (START) and the card prompt (A) to the main menu.
        s = wait(lambda s: s['scene'] == 1, 'reach the main menu', 360, KEY_START | KEY_A)
        time.sleep(3)
        capture('main-menu')
        check('main menu', True, str(s))
        # Main menu: down to VS, A. VS menu: Melee is hovered; A asks Solo/Group.
        for attempt in range(10):
            time.sleep(5)
            s = stub.state()
            if s['scene'] == 1 and s['menu'] == 2:
                break
            if s['scene'] == 1 and s['menu'] == 0:
                pulse(KEY_DOWN, 0.5)      # 1P is hovered first; VS is next
                time.sleep(2)
                pulse(KEY_A, 0.5)
            elif s['scene'] == 1 and s['menu'] != 0:
                print('in menu kind %d; B to the main menu' % s['menu'], flush=True)
                pulse(KEY_B, 0.5)
            time.sleep(3)
            s = stub.state()
        check('reached the VS menu', s['scene'] == 1 and s['menu'] == 2, str(s))
        capture('vs-menu')
        if args.top_menu:
            # Melee opens the top-screen Solo/Group menu (menu kind 11).
            pulse(KEY_A, 0.3)
            time.sleep(4)
            s = stub.state()
            check('Melee opens the Solo/Group menu (kind 11)', s['menu'] == 11, str(s))
            capture('solo-group')
            pulse(KEY_B, 0.3)
            time.sleep(4)
            s = stub.state()
            check('B returns to the VS menu', s['menu'] == 2, str(s))
            capture('after-back')
            # Again, down to Group, A: the Group list on the bottom screen.
            pulse(KEY_A, 0.3)
            time.sleep(4)
            pulse(KEY_DOWN, 0.3)
            time.sleep(1)
            capture('group-hovered')
            pulse(KEY_A, 0.3)
            time.sleep(3)
            capture('group-list')
            expect_log('Group opens the list', 'group: Group, looking for games')
        else:
            # Melee asks Solo or Group on the bottom screen (touch); the top screen stays on the VS menu.
            pulse(KEY_A, 0.3)
            time.sleep(4)
            s = stub.state()
            check('Melee shows the Solo/Group page (VS menu stays)', s['menu'] == 2, str(s))
            capture('solo-group')
            touch(53, 192)               # BACK
            time.sleep(2)
            expect_log('Back closes the page', 'group: Back from the Solo/Group page')
            capture('after-back')
            pulse(KEY_A, 0.3)
            time.sleep(4)
            touch(238, 104)              # GROUP
            time.sleep(3)
            capture('group-list')
            expect_log('Group opens the list', 'group: Group, looking for games')
        touch(118, 50)
        s = wait(lambda s: s['scene'] == 8, 'enter the lobby CSS', 120)
        check('Host enters the CSS in scene 8', s['mode'] == 8, str(s))
        time.sleep(8)
        capture('lobby-css')
        # B backs out of the CSS to the VS menu.
        for _ in range(3):
            pulse(KEY_B, 0.3)
        if stub.state()['scene'] != 1:
            for _ in range(3):
                pulse(KEY_B, 3.5)
        if stub.state()['scene'] != 1:
            # the hand to BACK (top right) with the circle pad, then A
            pulse(CPAD_UP | CPAD_RIGHT, 6)
            pulse(KEY_A, 0.3)
        s = wait(lambda s: s['scene'] == 1, 'back to the menus', 120)
        expect_log('Back from the CSS closes the session', 'group: left the lobby')
        time.sleep(4)
        capture('back-on-vs-menu')
        check('back on the VS menu with Melee hovered', s['menu'] == 2, f"menu kind {s['menu']}")
        # Melee -> Solo: the vanilla CSS (not the HANYU scene).
        pulse(KEY_A, 0.3)
        time.sleep(4)
        if args.top_menu:
            pulse(KEY_A, 0.3)   # Solo is hovered first
        else:
            touch(82, 104)      # SOLO
        s = wait(lambda s: s['scene'] == 8, 'Solo enters the CSS', 120)
        check('Solo is the vanilla VS CSS (mode != 8)', s['mode'] != 8, str(s))
        time.sleep(6)
        capture('solo-css')
    finally:
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        final = (out / 'game.log').read_text(errors='replace') if (out / 'game.log').exists() else ''
        for name, text in expected:
            check(name, text in final)
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
