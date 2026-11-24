"""Melee -> Solo / Group in Azahar drives the real VS menu of the development build.

Boots with group.ini menu=1 and the loopback link (udp=1), then: main menu -> VS -> Melee (the Solo/Group page),
Back (B), Melee again -> Group (the Group page), Host (the lobby CSS in the HANYU scene, with the connected-players strip),
B out of the CSS (back on the VS menu, the session closed), and finally Melee -> Solo (the vanilla CSS).
Every step saves both screens as PNGs and the log lines are checked.

    docker/emu-test.sh run tools/group/group_menu_test.py [--seconds 600]
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
sys.path.insert(0, str(HERE.parent / 'slippi'))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402

PORT = 24862
KEY_A, KEY_B, KEY_START, KEY_DOWN = 1, 2, 8, 0x80
KEY_UP = 0x40
CPAD_UP, CPAD_RIGHT = 1 << 30, 1 << 28
FIELDS = 'scene mode stock_mode teams seconds timer stage rule_stocks rule_minutes items stamina menu'.split()
MENU_MAIN, MENU_VS, SEL_MAIN_VS = 0, 2, 1   # src/melee/mn/forward.h


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

    # ---- driving the game by engine frames, so the tests work at full speed and when emulation is slow ----
    def frames(self):
        return struct.unpack('<I', self.peek('engine_frames', 4))[0]

    def until_frames(self, n, limit=120):
        """Wait until the engine has run n more frames (or `limit` seconds passed)."""
        start, end = self.frames(), time.monotonic() + limit
        while self.frames() - start < n and time.monotonic() < end:
            time.sleep(0.05)

    def press(self, keys, hold=2, release=8):
        """Hold keys for `hold` engine frames, then release them for `release` frames. Menus act on the press edge and
        ignore all input while their cooldown runs (5 frames after each action, mn_80229624), so callers check the
        result and press again rather than holding longer."""
        self.poke('mp_test_keys', keys)
        self.until_frames(hold)
        self.poke('mp_test_keys', 0)
        self.until_frames(release)

    def menu(self):
        """The main menus' state (MenuFlow mn_804A04F0, big-endian engine data): (menu kind, hovered row)."""
        b = self.peek('mn_804A04F0', 4)
        return b[0], (b[2] << 8) | b[3]

    def goto_vs_menu(self, timeout=600):
        """From the main menus to the VS menu by what the menus say, not by timing. Call it once the menus are up (scene
        1): the scene reads differently for a moment while a submenu loads, so this never presses START / A blindly."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            s = self.state()
            if s['scene'] != 1:
                time.sleep(0.2)
                continue
            kind, hovered = self.menu()
            print(f'  menu kind {kind}, hovered {hovered}', flush=True)
            if kind == MENU_VS:
                return s
            if kind == MENU_MAIN and hovered == SEL_MAIN_VS:
                self.press(KEY_A, 2, 30)
            elif kind == MENU_MAIN:
                # Up / Down repeat while held (each held frame can move): one frame per step.
                self.press(KEY_DOWN if hovered < SEL_MAIN_VS else KEY_UP, 1, 30)
            else:
                self.press(KEY_B, 2, 30)
        return self.state()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--seconds', type=float, default=900)
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--top-menu', action='store_true', help='the top-screen Solo/Group submenu (group.ini top_menu=1; its labels are still Slippi\'s Ranked / Unranked) instead of the bottom-screen touch page')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-menu'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text('menu=1\nudp=1\nport=44100\ndelay=2\n' + ('top_menu=1\n' if args.top_menu else ''))
    (sd / 'no-scripted-input').write_text('')   # no smoke-build scripted presses
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

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        stub.until_frames(10)

    def wait_log(text, count=1, timeout=120):
        """Wait until game.log has `text` at least `count` times."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if log_text().count(text) >= count:
                return True
            time.sleep(0.5)
        return False

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
                stub.press(press, 2, 30)
            else:
                time.sleep(1)
        check(what, False, f'last state {s}')
        raise SystemExit(1)

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        # Past the title (START) and the card prompt (A) to the main menu.
        s = wait(lambda s: s['scene'] == 1, 'reach the main menu', 360, KEY_START | KEY_A)
        stub.until_frames(30)
        capture('main-menu')
        check('main menu', True, str(s))
        # Main menu: down to VS, A. VS menu: Melee is hovered.
        s = stub.goto_vs_menu()
        check('reached the VS menu', s['scene'] == 1 and stub.menu()[0] == MENU_VS, str(s))
        stub.until_frames(30)
        capture('vs-menu')
        if args.top_menu:
            # Melee opens the top-screen Solo/Group menu (menu kind 11).
            stub.press(KEY_A)
            stub.until_frames(40)
            s = stub.state()
            check('Melee opens the Solo/Group menu (kind 11)', stub.menu()[0] == 11, str(stub.menu()))
            capture('solo-group')
            stub.press(KEY_B)
            stub.until_frames(40)
            check('B returns to the VS menu', stub.menu()[0] == MENU_VS, str(stub.menu()))
            capture('after-back')
            # Again, down to Group, A: the Group list on the bottom screen.
            stub.press(KEY_A)
            stub.until_frames(40)
            stub.press(KEY_DOWN, 1, 30)
            capture('group-hovered')
            stub.press(KEY_A)
            wait_log('group: Group, looking for games')
            capture('group-list')
            expect_log('Group opens the list', 'group: Group, looking for games')
        else:
            # Melee asks Solo or Group on the bottom screen (touch); the top screen stays on the VS menu.
            stub.press(KEY_A)
            wait_log('group: the Solo/Group page is up')
            stub.until_frames(10)
            s = stub.state()
            check('Melee shows the Solo/Group page (VS menu stays)', s['menu'] == 2, str(s))
            capture('solo-group')
            touch(53, 192)               # BACK
            wait_log('group: Back from the Solo/Group page')
            expect_log('Back closes the page', 'group: Back from the Solo/Group page')
            capture('after-back')
            stub.until_frames(20)
            stub.press(KEY_A)
            wait_log('group: the Solo/Group page is up', 2)
            touch(238, 104)              # GROUP
            wait_log('group: Group, looking for games')
            capture('group-list')
            expect_log('Group opens the list', 'group: Group, looking for games')
        touch(118, 50)
        s = wait(lambda s: s['scene'] == 8, 'enter the lobby CSS', 120)
        check('Host enters the CSS in scene 8', s['mode'] == 8, str(s))
        stub.until_frames(60)
        capture('lobby-css')
        # The engine must keep running in the lobby (a hang here once looked like B doing nothing).
        f0 = stub.frames()
        time.sleep(3)
        f1 = stub.frames()
        check('the engine keeps running on the lobby CSS', f1 > f0, f'engine_frames {f0} -> {f1}')
        # B backs out of the CSS to the VS menu: vanilla wants B released once, then held for 30 engine frames.
        stub.press(KEY_B, 2, 4)
        stub.poke('mp_test_keys', KEY_B)
        start, deadline = stub.frames(), time.monotonic() + 240
        # Let go as soon as the CSS is leaving (its pending scene byte is 2): B still held on the VS menu would go back
        # one more level, to the main menu.
        while time.monotonic() < deadline and stub.peek('mnCharSel_804D6CF6', 1)[0] != 2 and stub.frames() - start < 120:
            time.sleep(0.02)
        stub.poke('mp_test_keys', 0)
        s = wait(lambda s: s['scene'] == 1, 'back to the menus', 120)
        expect_log('Back from the CSS closes the session', 'group: left the lobby')
        stub.until_frames(60)
        capture('back-on-vs-menu')
        kind, hovered = stub.menu()
        check('back on the VS menu with Melee hovered', kind == MENU_VS and hovered == 0, f'menu kind {kind}, hovered {hovered}')
        # Melee -> Solo: the vanilla CSS (not the HANYU scene).
        stub.press(KEY_A)
        if args.top_menu:
            stub.until_frames(40)
            stub.press(KEY_A)   # Solo is hovered first
        else:
            wait_log('group: the Solo/Group page is up', 3)
            touch(82, 104)      # SOLO
        s = wait(lambda s: s['scene'] == 8, 'Solo enters the CSS', 120)
        check('Solo is the vanilla VS CSS (mode != 8)', s['mode'] != 8, str(s))
        stub.until_frames(60)
        capture('solo-css')
    finally:
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        if emu.log_path(home).exists():
            shutil.copy2(emu.log_path(home), out / 'azahar_log.txt')
        final = (out / 'game.log').read_text(errors='replace') if (out / 'game.log').exists() else ''
        for name, text in expected:
            check(name, text in final)
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
