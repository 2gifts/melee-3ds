"""After a group match: the results screen, sudden death, stars and auto handicap, with two emulated consoles.

Console A hosts (Melee -> Group -> Host) with the rules set to a 1 minute time match and handicap Auto; console B
joins. Both take Fox and nobody moves, so the match is tied and a sudden death follows (on both). The host is held
at a breakpoint as its sudden death ends, so B's results screen is up first: B's press there must be kept and applied
once the host's results screen is up. Then, on both screens: B's Start toggles B's "ready" both ways, A's Start makes A
ready, and both screens end together once both are. Back on the character select: the winner's star on both consoles,
the auto handicap (winner one step down, last place one up) on both, and each console's save holding only its own
door's value. B leaves and rejoins: the star is still there. A second match (A and the CPU B's door became) is quit
from the pause menu: its results go through the cancelled-match skip, and nothing changes. Then Back on the host,
answered "Stay" many times (the "End the session?" dialog), and finally "End it": the character select keeps working
and the session ends cleanly; out of the session the stars are gone.
Names: the consoles are named Anna and Beeb (group.ini name=); under the merged results' windows both
screens show those names instead of "FOX"; on match 2's results (B's door a CPU) A's window keeps Anna and B's window
its fighter's name. tools/group/group_names_test.py covers drops, leaving and late joiners.

    docker/emu-test.sh run tools/group/group_results_test.py [--show]
Results: $MELEE_TEST_OUT/group-results/ (PNGs, both game.logs)."""
import argparse
import os
import shutil
import socket
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'slippi'))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from css_probe import Css  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402
from names_probe import labels, sis  # noqa: E402
from group_menu_test import (CPAD_RIGHT, CPAD_UP, KEY_A, KEY_B, KEY_START, KEY_UP, MENU_VS, Stub, save_bottom,  # noqa: E402
                             save_top, to_main_menu)

KEY_L, KEY_R = 1 << 9, 1 << 8
CPAD_LEFT, CPAD_DOWN = 1 << 29, 1 << 31
FOX_TILE = (-24.4, -17.4, 6.0, 13.0)   # left, right, bottom, top in CSS units (mncharsel.c icons[], a starter)
LINK_PORT = 44160
RULES = 0x1850                          # gmm_x0.x1850: the VS rules (GameRules)
NAMETAG_HC = 0x588                      # gmm_x0.modes.nametags[4]: the handicap of a port with no name tag
DIALOG_YES, DIALOG_NO = (89, 137), (231, 137)   # "End the session?": END IT / STAY (bottom_draw.c GD_*)


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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--back-presses', type=int, default=25)
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-results'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    a = Console('A', 24871, sym, out, args.dsx, args.show, 'name=Anna\n')
    time.sleep(3)
    b = Console('B', 24872, sym, out, args.dsx, args.show, 'name=Beeb\n')

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, ('' if ok else detail) if detail else '', flush=True)
        return ok

    def wait_log(c, text, what, timeout=180, count=1, fatal=True):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if c.log().count(text) >= count:
                return check(what, True)
            if c.proc.poll() is not None:
                break
            time.sleep(0.5)
        check(what, False, f'console {c.name}: not in game.log ({count}x): {text}')
        if fatal:
            raise SystemExit(1)
        return False

    def wait_scene(c, scene, what, timeout=180, fatal=True):
        end = time.monotonic() + timeout
        s = None
        while time.monotonic() < end:
            try:
                s = c.scene()
            except Exception:  # noqa: BLE001
                time.sleep(1)
                continue
            if s == scene:
                return check(what, True)
            time.sleep(0.5)
        check(what, False, f'console {c.name}: scene {s}')
        if fatal:
            raise SystemExit(1)
        return False

    def steps(c, since=0):
        """The results screen's log since `since`: [(step, ready mask)]."""
        found = []
        for line in c.log()[since:].splitlines():
            if 'Group: results step ' in line:
                w = line.split('Group: results step ')[1].replace(',', '').split()
                found.append((int(w[0]), int(w[2], 16)))
        return found

    def screen(c, since=0):
        """The screen's step and the players' ready bits (ports 1 and 2: vanilla counts the empty ports as ready)."""
        s = steps(c, since)
        return (s[-1][0], s[-1][1] & 0x3) if s else (None, None)

    def wait_screen(c, pred, what, since=0, timeout=120, fatal=False):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if pred(screen(c, since)):
                return check(what, True)
            time.sleep(0.3)
        check(what, False, f'console {c.name}: results log {steps(c, since)[-6:]}')
        if fatal:
            raise SystemExit(1)
        return False

    def to_group_page(c):
        s = c.stub.goto_vs_menu()
        check(f'{c.name}: reached the VS menu', s['scene'] == 1 and c.stub.menu()[0] == MENU_VS, str(c.stub.menu()))
        for _ in range(8):
            if c.stub.menu()[1] == 0:
                break
            c.stub.press(KEY_UP)
        n = c.log().count('group: the Solo/Group page is up')
        c.stub.press(KEY_A)
        wait_log(c, 'group: the Solo/Group page is up', f'{c.name}: the Solo/Group page opens', 60, n + 1)
        n = c.log().count('group: Group, looking for games')
        c.touch(238, 104)                # GROUP
        wait_log(c, 'group: Group, looking for games', f'{c.name}: Group opens the Host / Join page', 60, n + 1)

    def join(c, what):
        c.stub.until_frames(120)         # a few scans, so the game is in the list
        c.touch(160, 80)                 # the first game in the list (both layouts)
        wait_scene(c, 8, what, 120)
        c.stub.until_frames(120)

    def token(c, door):
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BD0', 16)[4 * door:4 * door + 4])[0]
        bts = c.stub.read(ptr, 0x10)
        x, y = struct.unpack('>ff', bts[8:16])
        return bts[5], x, y

    def take_fox(c, door):
        """This console's token onto Fox, then A (the door's fighter is placed)."""
        left, right, bottom, top = FOX_TILE
        cx, cy = (left + right) / 2, (bottom + top) / 2

        def placed():
            return c.css.cursor(door)[0] != 1 and c.css.doors()[door]['sel_icon'] < 0x19
        for _ in range(4):
            if placed():
                break
            for _ in range(300):
                _, x, y = token(c, door)
                if left + 0.8 < x < right - 0.8 and bottom + 0.8 < y < top - 0.8:
                    break
                keys = 0
                if abs(x - cx) > 0.6:
                    keys |= CPAD_RIGHT if x < cx else CPAD_LEFT
                if abs(y - cy) > 0.6:
                    keys |= CPAD_UP if y < cy else CPAD_DOWN
                c.stub.press(keys, 1, 1)
            c.stub.press(KEY_A, 4, 30)
        return check(f'{c.name}: the token is down on Fox', placed(), f'cursor {c.css.cursor(door)}, door {c.css.doors()[door]}')

    def start_match(n):
        """A presses Start on the CSS and picks a stage; both consoles' match n starts."""
        started = {c.name: c.log().count('Slippi online: match starts') for c in (a, b)}
        asked, picked = a.log().count('GROUPCSS Start pressed'), a.log().count('group: starting a match')
        for _ in range(8):
            a.stub.press(KEY_START, 4, 30)
            if a.log().count('GROUPCSS Start pressed') > asked:
                break
        wait_log(a, 'GROUPCSS Start pressed', f'match {n}: Start asks the lobby', 60, asked + 1)
        end = time.monotonic() + 180
        while time.monotonic() < end and a.scene() == 8:
            time.sleep(0.5)
        for _ in range(8):               # the stage cursor starts below the grid: up onto a stage, then A
            if a.log().count('group: starting a match') > picked:
                break
            a.stub.press(CPAD_UP, 6, 4)
            a.stub.press(KEY_A, 4, 60)
        wait_log(a, 'group: starting a match', f'match {n}: the host starts the match', 120, picked + 1)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'match {n}: {c.name}\'s match starts', 240, started[c.name] + 1)

    def press(c, keys, frames=4):
        c.stub.press(keys, frames, 20)

    try:
        time.sleep(15)
        for c in (a, b):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))

        # The host's rules: a 1 minute time match with handicap Auto (the VS rules in A's save, as its Rules menu
        # would set them).
        before = a.save(RULES, 8)
        a.stub.write(sym['gmMainLib_8045A6C0'] + RULES + 2, bytes([0, 1]))      # mode time, 1 minute
        a.stub.write(sym['gmMainLib_8045A6C0'] + RULES + 5, bytes([1]))         # handicap auto
        print('A rules before', before.hex(), 'after', a.save(RULES, 8).hex(), flush=True)

        to_group_page(a)
        a.touch(118, 50)                 # HOST
        wait_scene(a, 8, 'A hosts: the lobby character select')
        to_group_page(b)
        join(b, 'B joins A\'s lobby')
        wait_log(b, "GROUPCSS the host's rules: mode 0 time 1", 'B takes the host\'s rules (a 1 minute time match)', 60)
        wait_log(b, 'handicap 1', 'B takes the host\'s handicap Auto', 10)
        saves = {c.name: list(c.save(NAMETAG_HC, 4)) for c in (a, b)}
        check('a new session sets every no-tag port\'s handicap to 5 (both saves)',
              saves['A'] == [5, 5, 5, 5] and saves['B'] == [5, 5, 5, 5], str(saves))
        take_fox(a, 0)
        take_fox(b, 1)
        for c in (a, b):
            c.stub.until_frames(90)
        a.capture('01-css')

        # ---- match 1: a tie, a sudden death, the results ----
        start_match(1)
        wait_log(a, 'Group: the match was tied', 'match 1 is tied on A (nobody moved): sudden death', 400)
        wait_log(b, 'Group: the match was tied', 'match 1 is tied on B too', 60)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'the sudden death starts on {c.name}', 240, 2)
        # Hold the host as its sudden death ends (group_game_end publishes its results screen to the lobby).
        print('breakpoint:', a.stub.hold_at('group_game_end'), flush=True)
        if not a.stub.held(600):
            check('the host reaches the end of the sudden death', False)
            raise SystemExit(1)
        wait_screen(b, lambda s: s[0] == 2, 'B\'s results screen waits for a press while the host is held', 0, 120, True)
        mark = len(b.log())
        press(b, KEY_A)
        wait_log(b, 'results press kept until the host\'s results screen (step 2)', 'B\'s early press is kept', 30, 1, False)
        time.sleep(2)
        check('B\'s screen does not move on by itself while the host is held', screen(b)[0] == 2, str(steps(b)[-3:]))
        a.stub.release()
        a_mark = len(a.log())
        wait_screen(a, lambda s: s[0] == 3, 'the host\'s screen moves past its intro with no press of its own', a_mark, 180)
        wait_screen(b, lambda s: s[0] == 3, 'B\'s screen moves on once the host\'s results are up', mark, 180)
        stub_frames = a.stub.frames()
        while a.stub.frames() - stub_frames < 120:   # the panels come in
            time.sleep(0.2)
        a.capture('02-results')
        b.capture('02-results')
        want = [sis('Anna'), sis('Beeb'), b'', b'']
        for c in (a, b):
            got, named = labels(c.stub)
            check(f'{c.name}: the merged results show Anna and Beeb under the windows, not the fighter',
                  got == want and named == 0x3, f'{got}, named {named:x}')

        # B toggles its ready both ways, A gets ready, then B: both screens end together.
        press(b, KEY_START)
        wait_screen(a, lambda s: s == (3, 0x2), 'B\'s Start shows B ready on A\'s screen', a_mark)
        wait_screen(b, lambda s: s == (3, 0x2), 'B\'s Start shows B ready on B\'s screen', mark)
        press(b, KEY_START)
        wait_screen(a, lambda s: s == (3, 0x0), 'B\'s second Start takes it back on A\'s screen', a_mark)
        wait_screen(b, lambda s: s == (3, 0x0), 'B\'s second Start takes it back on B\'s screen', mark)
        press(a, KEY_START)
        wait_screen(a, lambda s: s == (3, 0x1), 'A\'s Start shows A ready on A\'s screen', a_mark)
        wait_screen(b, lambda s: s == (3, 0x1), 'A\'s Start shows A ready on B\'s screen', mark)
        time.sleep(3)
        check('one player ready: neither screen ends', screen(a, a_mark)[0] == 3 and screen(b, mark)[0] == 3)
        press(b, KEY_START)
        wait_scene(a, 8, 'both ready: A is back on the character select', 180, False)
        wait_scene(b, 8, 'both ready: B is back on the character select', 180, False)
        for c in (a, b):
            c.stub.until_frames(120)
        a.capture('03-css-after')
        b.capture('03-css-after')

        # Stars: vanilla's count (each Human player's KOs of the others, gmVsMelee_UpdateKOCounts), the host's, on both
        # consoles. Nobody moved, so the sudden death's bob-ombs decide it and nobody is credited a KO.
        over = [l for l in a.log().splitlines() if 'group: the results are over: stars' in l]
        host_stars = [int(x) for x in over[-1].split('stars ')[1].split('/')] if over else None
        stars_a, stars_b = a.stars(), b.stars()
        check('the host counts the stars (no KO was credited: none)', host_stars == [0, 0, 0, 0], str(over[-1:]))
        check('both consoles show the host\'s stars', stars_a == host_stars and stars_b == host_stars,
              f'A {stars_a}, B {stars_b}, host {host_stars}')

        # Auto handicap: the winner one step down, last place one up; the same on both; each save only its own door.
        hcs = {}
        for c in (a, b):
            line = [l for l in c.log().splitlines() if 'auto handicap after the match' in l]
            hcs[c.name] = line[-1].split('match: ')[1].strip() if line else None
        check('both consoles compute the same auto handicap', hcs['A'] is not None and hcs['A'] == hcs['B'], str(hcs))
        got = None
        if hcs['A']:
            w = hcs['A'].split()[0:2]
            v = int(w[0], 16) << 16 | int(w[1], 16)
            got = [v >> 24 & 255, v >> 16 & 255, v >> 8 & 255, v & 255]
        winner = 1 if got and got[1] < got[0] else 0   # the sudden death's winner: who it was is up to the bob-ombs
        loser = 1 - winner
        want = [5, 5, 0, 0]
        want[winner], want[loser] = 4, 6
        print('the sudden death\'s winner: port', winner + 1, flush=True)
        check('auto handicap: winner 5 -> 4, last place 5 -> 6, empty doors none', got == want, f'{got} != {want}')
        sa, sb = list(a.save(NAMETAG_HC, 4)), list(b.save(NAMETAG_HC, 4))
        check('A\'s save holds only its own door\'s new handicap', sa == [want[0], 5, 5, 5], str(sa))
        check('B\'s save holds only its own door\'s new handicap', sb == [5, want[1], 5, 5], str(sb))
        for c in (a, b):
            ks = [c.css.knob(d, 'cpuslider_joint') for d in (0, 1)]
            check(f'{c.name}: both doors\' handicap sliders show the new values',
                  all(k is not None and abs(k - 1.25 * (want[d] - 1)) < 0.05 for d, k in enumerate(ks)), str(ks))

        # B leaves and comes back: the star stays.
        # Back held (as group_join_test: a tap only lifts the token) until the CSS takes it
        b.stub.press(KEY_B, 2, 4)
        b.stub.poke('mp_test_keys', KEY_B)
        start, end = b.stub.frames(), time.monotonic() + 120
        while time.monotonic() < end and b.stub.peek('mnCharSel_804D6CF6', 1)[0] != 2 and b.stub.frames() - start < 180:
            time.sleep(0.05)
        b.stub.poke('mp_test_keys', 0)
        wait_log(b, 'group: left the lobby', 'B leaves the lobby', 60)
        a.stub.until_frames(120)
        check('the stars stay on the host when B leaves', a.stars() == host_stars, str(a.stars()))
        to_group_page(b)
        join(b, 'B rejoins')
        check('B sees the stars again after rejoining', b.stars() == host_stars, f'{b.stars()} != {host_stars}')
        b.capture('04-rejoined')

        # ---- match 2: quit from the pause menu (a cancelled match) ----
        saves_before = {c.name: list(c.save(NAMETAG_HC, 4)) for c in (a, b)}
        hc_lines = a.log().count('auto handicap after the match')
        a_mark, b_mark = len(a.log()), len(b.log())
        start_match(2)
        a.stub.until_frames(300)
        a.stub.poke('mp_test_keys', KEY_START)
        a.stub.until_frames(6)
        a.stub.poke('mp_test_keys', 0)
        a.stub.until_frames(30)
        a.stub.poke('mp_test_keys', KEY_L | KEY_R | KEY_A | KEY_START)
        wait_log(a, 'Group: the results screen', 'match 2 quit: A goes to its results screen', 240, 2)
        a.stub.poke('mp_test_keys', 0)
        wait_log(b, 'Group: the results screen', 'B goes to its results screen', 240, 2)
        wait_screen(a, lambda s: s[0] is not None, 'A\'s results screen is up', a_mark, 60)
        print('A after the quit:', steps(a, a_mark)[:4], 'B:', steps(b, b_mark)[:4], flush=True)
        check('a cancelled match: the screens start at the wait a press may skip',
              steps(a, a_mark)[:1] == [(0, 0)] and steps(b, b_mark)[:1] == [(0, 0)], f'A {steps(a, a_mark)[:2]}, B {steps(b, b_mark)[:2]}')
        # B's door is a CPU now (it left during the CSS): only A's Start moves this screen on.
        for _ in range(20):
            if any(st >= 3 for st, _ in steps(a, a_mark)):
                break
            press(a, KEY_START)
            time.sleep(0.5)
        b.stub.until_frames(60)
        got, named = labels(a.stub)
        check('match 2 (B\'s door a CPU): A\'s window shows Anna, the CPU\'s keeps its fighter\'s name',
              got == [sis('Anna'), b'', b'', b''] and named == 0x1, f'{got}, named {named:x}')
        a.capture('04b-results-cpu')
        check('a cancelled match: A\'s Start skips to the panels on both screens',
              any(st >= 3 for st, _ in steps(a, a_mark)) and any(st >= 3 for st, _ in steps(b, b_mark)),
              f'A {steps(a, a_mark)}, B {steps(b, b_mark)}')
        for _ in range(3):
            if a.scene() == 8:
                break
            if screen(a, a_mark)[0] == 3 and not screen(a, a_mark)[1] & 1:
                press(a, KEY_START)
            time.sleep(10)
        wait_scene(a, 8, 'match 2: A is back on the character select', 180, False)
        wait_scene(b, 8, 'match 2: B is back on the character select', 180, False)
        for c in (a, b):
            c.stub.until_frames(120)
        check('a cancelled match changes no handicap', a.log().count('auto handicap after the match') == hc_lines)
        saves_after = {c.name: list(c.save(NAMETAG_HC, 4)) for c in (a, b)}
        check('a cancelled match changes no save', saves_after == saves_before, f'{saves_before} -> {saves_after}')
        check('both consoles still show the same stars', a.stars() == b.stars(), f'A {a.stars()}, B {b.stars()}')

        # ---- Back on the host, answered "Stay", many times; then "End it" ----
        panics = a.log().count('PANIC')

        def dialog():
            return int.from_bytes(a.stub.peek('dialog', 4), 'little')   # group_menu.c: "End the session?" is up

        def ask_to_leave():
            """Back held until the dialog is up (a tap only lifts the token); True once it is."""
            a.stub.poke('mp_test_keys', KEY_B)
            start = a.stub.frames()
            while not dialog() and a.stub.frames() - start < 180:
                time.sleep(0.05)
            a.stub.poke('mp_test_keys', 0)
            a.stub.until_frames(10)
            return bool(dialog())
        stays = 0
        for i in range(args.back_presses):
            if not ask_to_leave():
                break
            a.touch(*DIALOG_NO)
            a.stub.until_frames(20)
            if a.scene() != 8 or dialog():
                break
            stays += 1
        a.capture('05-after-stays')
        check(f'Back answered "Stay" {args.back_presses} times: A stays on a working character select',
              stays == args.back_presses and a.scene() == 8 and a.log().count('PANIC') == panics and a.proc.poll() is None,
              f'{stays} stays, scene {a.scene()}')
        take_ok = a.css.doors()[0]['p_kind'] == 0
        check('A\'s door is still its own after the Back presses', take_ok, str(a.css.doors()[0]))
        check('Back asks "End the session?" once more', ask_to_leave())
        # HOME and sleep were off on the host from Host on, through both matches and their results
        check('A: HOME and sleep off through the matches and results (one off, no on)',
              a.log().count('group: HOME and sleep off') == 1 and 'group: HOME and sleep on' not in a.log())
        a.touch(*DIALOG_YES)
        wait_scene(a, 1, '"End it": A is back on the menus', 120, False)
        wait_log(a, 'group: HOME and sleep on', 'A: HOME and sleep on again once the session ends', 60)
        check('ending the session does not crash A', a.proc.poll() is None and a.log().count('PANIC') == panics)
        wait_scene(b, 1, 'B goes back to the menus as the session ends', 120, False)
        wait_log(b, 'group: HOME and sleep on', 'B: HOME and sleep on again once the session ends', 60, 2)
        check('out of the session the stars are gone (A)', a.stars() == [0, 0, 0, 0], str(a.stars()))
    finally:
        for c in (a, b):
            if c.stub.held_at is not None:
                try:
                    c.stub.release()
                except Exception:  # noqa: BLE001
                    pass
            emu.stop(c.proc)
        time.sleep(1)
        for c in (a, b):
            if (c.sd / 'game.log').exists():
                shutil.copy2(c.sd / 'game.log', out / f'game-{c.name}.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
