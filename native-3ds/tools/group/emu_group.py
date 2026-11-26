"""The emulated consoles of group play's Azahar tests (tools/group/*_test.py): the GDB stub (Stub, PortStub), one
console with its own SD card and group.ini (Console), the screenshots, and the steps every test takes (the main menu,
"End the session?").

Each console reads its memory through Azahar's GDB stub while it runs (tools/gdb_probe.py), by the development build's
symbols (emu.elf_symbols). Nothing presses buttons by itself in these builds (no-scripted-input): the tests drive the
console through test hooks the development build has (mp_test_pad, mp_test_bottom_touch, mp_test_capture, ...).
"""
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
from css_probe import Css  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402

PORT = 24862
KEY_L, KEY_R = 1 << 9, 1 << 8
SCENE_CHOOSE = 36   # the tournament setup scene: "Choose a game." (group_lobby_screen.py)
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
    """The emulated console's memory through Azahar's GDB stub. One connection stays open and the console keeps
    running: Azahar answers memory reads and writes without stopping it. Stopping it for each access (Ctrl-C or a new
    connection, then 'c') costs far more emulated time than the access: polling every 50 ms left the console at about
    one frame a second, so the tests timed out or hung."""

    def __init__(self, sym):
        self.sym = sym
        self.sock = None

    def _close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def _session(self, fn):
        last = None
        for _ in range(10):
            try:
                if self.sock is None:
                    s = socket.create_connection(('127.0.0.1', PORT), 5)
                    s.settimeout(10)
                    self.sock = s
                    packet(s, '?')               # connecting stops the console: this is its stop reply
                    receive(s)
                    packet(s, 'c')               # and it runs on from here
                return fn(self.sock)
            except (OSError, TimeoutError, ValueError, ConnectionError) as e:
                last = e
                self._close()
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

    def choose(self, group=True):
        """Solo or Group, once Melee was pressed: on the top-screen submenu (menu kind 11) with Up / Down and A; without
        it (group.ini top_menu=0, or no menu art) by a touch on the bottom-screen page. Menus ignore presses during
        their cooldown, so Up / Down is pressed until the row moves."""
        self.until_frames(30)
        if self.menu()[0] != 11:
            self.poke('mp_test_bottom_touch', (238 if group else 82) | (104 << 16))
            self.until_frames(10)
            return
        want = 1 if group else 0
        for _ in range(10):
            if self.menu()[1] == want:
                break
            self.press(KEY_DOWN if want else KEY_UP, 1, 30)
        self.press(KEY_A, 2, 10)

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


def to_main_menu(stub, timeout=360):
    """Past the title (START) and the memory card prompt (A) to the main menus (scene 1), as a player would: nothing
    presses them by itself (no-scripted-input)."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if stub.state()['scene'] == 1:
            return True
        stub.press(KEY_START | KEY_A, 2, 30)
    return False


CPAD_LEFT, CPAD_DOWN = 1 << 29, 1 << 31
FOX_TILE = (-24.4, -17.4, 6.0, 13.0)   # left, right, bottom, top in CSS units (mncharsel.c icons[], a starter)
LINK_PORT = 44160
RULES = 0x1850                          # gmm_x0.x1850: the VS rules (GameRules)
NAMETAG_HC = 0x588                      # gmm_x0.modes.nametags[4]: the handicap of a port with no name tag
KEY_LEFT = 0x20


def dialog_up(c):
    """1 while "End the session?" is up (group_menu.c's flag; the box itself is the engine's, group_dialog.c)."""
    return int.from_bytes(c.stub.peek('dialog', 4), 'little')


def answer_dialog(c, end_it, with_a=False):
    """Answer the top-screen "End the session?" with the buttons, as a player: Left then A ends the session; B (or A,
    since No is highlighted first) stays."""
    if end_it:
        c.stub.press(KEY_LEFT, 2, 10)
        c.stub.press(KEY_A, 2, 20)
    else:
        c.stub.press(KEY_A if with_a else KEY_B, 2, 20)


def end_session_if_asked(c):
    """After Back on the host's character select: "Yes" if the dialog asked."""
    c.stub.until_frames(15)
    if dialog_up(c):
        answer_dialog(c, True)


class PortStub(Stub):
    """group_menu_test's Stub on a GDB port of its own (one per emulated console), with raw memory access and a
    breakpoint that holds the console."""

    def __init__(self, sym, port):
        super().__init__(sym)
        self.port = port
        self.held_at = None

    def _session(self, fn):
        last = None
        for _ in range(10):
            try:
                if self.sock is None:
                    s = socket.create_connection(('127.0.0.1', self.port), 5)
                    s.settimeout(10)
                    self.sock = s
                    packet(s, '?')
                    receive(s)
                    packet(s, 'c')
                return fn(self.sock)
            except (OSError, TimeoutError, ValueError, ConnectionError) as e:
                last = e
                self._close()
                time.sleep(1)
        raise last

    def read(self, addr, n):
        def f(s):
            packet(s, f'm{addr:x},{n:x}')
            return bytes.fromhex(receive(s))
        return self._session(f)

    def write(self, addr, data):
        def f(s):
            packet(s, f'M{addr:x},{len(data):x}:' + data.hex())
            receive(s)
        self._session(f)

    def hold_at(self, name):
        """A breakpoint on `name`: the console stops when it gets there. Nothing else may use this stub until
        held() said so (the stop arrives as a packet of its own)."""
        addr = self.sym[name] & ~1

        def f(s):
            packet(s, f'Z0,{addr:x},4')
            return receive(s)
        r = self._session(f)
        self.held_at = addr
        return r

    def held(self, seconds):
        """True once the console stopped at the breakpoint (waits up to `seconds`)."""
        end = time.monotonic() + seconds
        self.sock.settimeout(1)
        try:
            while time.monotonic() < end:
                try:
                    r = receive(self.sock)
                except (socket.timeout, TimeoutError):
                    continue
                if r.startswith(('T', 'S')):
                    return True
            return False
        finally:
            self.sock.settimeout(10)

    def release(self):
        """Remove the breakpoint and let the console run on."""
        def f(s):
            packet(s, f'z0,{self.held_at:x},4')
            receive(s)
            packet(s, 'c')
        self._session(f)
        self.held_at = None


class Console:
    def __init__(self, name, gdb_port, sym, out, dsx, show, ini='', prefix='group-results', link_port=None):
        self.name, self.out = name, out
        self.home = emu.prepare(f'{prefix}-{name}', gdb_port)
        self.sd = emu.sd(self.home)
        (self.sd / 'slippi').mkdir(parents=True, exist_ok=True)
        (self.sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
        (self.sd / 'slippi' / 'group.ini').write_bytes(
            (ini + f'menu=1\nudp=1\nport={link_port or LINK_PORT}\ndelay=2\ntimeout_ms=90000\n').encode())   # ini's keys first: they win
        (self.sd / 'no-scripted-input').write_text('')   # no smoke-build scripted presses
        (self.sd / 'game.log').unlink(missing_ok=True)
        self.stub = PortStub(sym, gdb_port)
        self.css = Css(self.stub)
        self.proc = emu.launch(self.home, Path(dsx).resolve(), hidden=not show)

    def log(self):
        p = self.sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def touch(self, x, y):
        self.stub.poke('mp_test_bottom_touch', x | (y << 16))
        self.stub.until_frames(20)

    def capture(self, name):
        st = self.stub
        st.poke('mp_test_capture', 1)
        for _ in range(120):
            time.sleep(0.5)
            if int.from_bytes(st.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(self.sd / 'engine-top.bgr', self.out / f'{name}-{self.name}-top.png')
        save_bottom(self.sd / 'engine-bottom.rgb565', self.out / f'{name}-{self.name}-bottom.png')

    def save(self, offset, n):
        return self.stub.read(self.stub.sym['gmMainLib_8045A6C0'] + offset, n)

    def stars(self):
        return list(self.stub.peek('ko_counts', 4))

    def scene(self):
        return self.stub.state()['scene']
