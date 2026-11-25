"""The names under the group results screen's windows, with three emulated consoles and a PC console.

A hosts (named Anna), B joins (Zoë Ångström: accented letters, and over the 10-character limit), C joins (WWWWWWWWWW:
the widest ten characters). All three take Fox, the host makes door 4 a CPU Fox (its HMN / CPU button, then its coin onto
Fox), and starts the match; the CPU's window keeps vanilla's "FOX", for comparison. C's emulator is stopped during it: A and B make
C's fighter a CPU. A quits from the pause menu (a cancelled match), and on the results screen of A and B the windows
show Anna, "Zoe Angstr" (folded, 10 characters) and WWWWWWWWWW instead of FOX: C keeps its name although its fighter ended as a CPU (screenshots).
Then B's emulator is stopped on the results screen: A's labels do not change. A PC console, Dee, named with ten kana
(ゆうきゆうきゆうきゆ, Phase 6: kana in the game's font) joins during the
results and takes a free port in the roster (B's or C's): A's labels still do not change. A presses through; back on
the character select. Match 2 (A, Dee, and the CPU C's door became; the PC console cannot reproduce the game's
checksums, so it ends early on a desync): A's results show Anna and Dee's ten kana, and the CPU's window its fighter's name.

    docker/emu-test.sh run tools/group/group_names_test.py [--show] [--tune]
--tune: on the first results screen, capture the names at a few sizes and offsets (mp_res_size / mp_res_dy).
Results: $MELEE_TEST_OUT/group-names/ (PNGs, the game.logs, the PC console's log)."""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'slippi'))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from group_menu_test import CPAD_RIGHT, CPAD_UP, KEY_A, KEY_START, KEY_UP, MENU_VS, to_main_menu  # noqa: E402
from group_results_test import Console, FOX_TILE, KEY_L, KEY_R, CPAD_LEFT, CPAD_DOWN  # noqa: E402
from names_probe import labels, sis, wire_name  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44180
NAMES = {'A': 'Anna', 'B': 'Zoë Ångström', 'C': 'WWWWWWWWWW'}
DEE = '\u3086\u3046\u304d' * 3 + '\u3086'   # ゆうきゆうきゆうきゆ


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--tune', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-names'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    consoles = {}
    for k, (name, port) in enumerate((('A', 24881), ('B', 24882), ('C', 24883))):
        # timeout_ms: C must be dropped well within the match (emulation stalls a few seconds now and then)
        consoles[name] = Console(name, port, sym, out, args.dsx, args.show, f'name={NAMES[name]}\ntimeout_ms=20000\n',
                                 'group-names', LINK_PORT)
        time.sleep(3)
    a, b, c3 = consoles['A'], consoles['B'], consoles['C']
    pc_proc = None
    want = [sis(wire_name(NAMES[n])) for n in 'ABC'] + [b'']

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, ('' if ok else detail) if detail else '', flush=True)
        return ok

    def text_of(c):
        return c.log() if isinstance(c, Console) else ((out / 'pc.log').read_text(errors='replace') if (out / 'pc.log').exists() else '')

    def wait_log(c, text, what, timeout=180, count=1, fatal=True):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text_of(c).count(text) >= count:
                return check(what, True)
            time.sleep(0.5)
        check(what, False, f'not in the log ({count}x): {text}')
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
        found = []
        for line in c.log()[since:].splitlines():
            if 'Group: results step ' in line:
                w = line.split('Group: results step ')[1].replace(',', '').split()
                found.append((int(w[0]), int(w[2], 16)))
        return found

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
        c.stub.until_frames(120)
        c.touch(160, 80)                 # the first game in the list (both layouts)
        wait_scene(c, 8, what, 120)
        c.stub.until_frames(120)

    def token(c, door):
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BD0', 16)[4 * door:4 * door + 4])[0]
        bts = c.stub.read(ptr, 0x10)
        x, y = struct.unpack('>ff', bts[8:16])
        return bts[5], x, y

    def steer_cursor(c, target, inside, steps=300):
        """Move c's own cursor (port 1's: only the host uses it, with an empty hand) until inside(x, y)."""
        for _ in range(steps):
            _, x, y = c.css.cursor(0)
            if inside(x, y):
                return True
            keys = 0
            if abs(x - target[0]) > 0.5:
                keys |= CPAD_RIGHT if x < target[0] else CPAD_LEFT
            if abs(y - target[1]) > 0.5:
                keys |= CPAD_UP if y < target[1] else CPAD_DOWN
            c.stub.press(keys, 1, 1)
        return False

    def take_fox(c, door, cpu=False):
        left, right, bottom, top = FOX_TILE
        cx, cy = (left + right) / 2, (bottom + top) / 2

        def placed():
            return (cpu or c.css.cursor(door)[0] != 1) and c.css.doors()[door]['sel_icon'] < 0x19
        for _ in range(1 if cpu else 4):
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

    def start_match(n, players):
        started = {c.name: c.log().count('Slippi online: match starts') for c in players}
        asked, picked = a.log().count('GROUPCSS Start pressed'), a.log().count('group: starting a match')
        for _ in range(8):
            a.stub.press(KEY_START, 4, 30)
            if a.log().count('GROUPCSS Start pressed') > asked:
                break
        wait_log(a, 'GROUPCSS Start pressed', f'match {n}: Start asks the lobby', 60, asked + 1)
        end = time.monotonic() + 180
        while time.monotonic() < end and a.scene() == 8:
            time.sleep(0.5)
        for _ in range(8):
            if a.log().count('group: starting a match') > picked:
                break
            a.stub.press(CPAD_UP, 6, 4)
            a.stub.press(KEY_A, 4, 60)
        wait_log(a, 'group: starting a match', f'match {n}: the host starts the match', 120, picked + 1)
        for c in players:
            wait_log(c, 'Slippi online: match starts', f'match {n}: {c.name}\'s match starts', 240, started[c.name] + 1)

    def quit_match(c):
        """Pause, then L+R+A+Start: a cancelled match, which goes to the results."""
        c.stub.poke('mp_test_keys', KEY_START)
        c.stub.until_frames(6)
        c.stub.poke('mp_test_keys', 0)
        c.stub.until_frames(30)
        c.stub.poke('mp_test_keys', KEY_L | KEY_R | KEY_A | KEY_START)

    def to_panels(c, mark, pressing):
        """Wait until c's results screen shows the players' panels (a cancelled match's wait is skipped by the host's
        Start)."""
        end = time.monotonic() + 240
        while time.monotonic() < end:
            if any(st >= 3 for st, _ in steps(c, mark)):
                c.stub.until_frames(150)   # the panels slide in
                return check(f'{c.name}: the results screen shows the players\' panels', True)
            if pressing is not None and not any(st >= 3 for st, _ in steps(pressing, marks[pressing.name])):
                pressing.stub.press(KEY_START, 4, 20)
            time.sleep(1)
        return check(f'{c.name}: the results screen shows the players\' panels', False, str(steps(c, mark)[-5:]))

    def put_float(c, name, v):
        c.stub.write(sym[name], struct.pack('>f', v))

    marks = {}
    try:
        time.sleep(15)
        for c in (a, b, c3):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))
        to_group_page(a)
        a.touch(118, 50)                 # HOST
        wait_scene(a, 8, 'A hosts: the lobby character select')
        to_group_page(b)
        join(b, 'B joins A\'s lobby')
        to_group_page(c3)
        join(c3, 'C joins A\'s lobby')
        for door, c in enumerate((a, b, c3)):
            take_fox(c, door)
        for c in (a, b, c3):
            c.stub.until_frames(90)
        # Door 4: a CPU (the host's cursor clicks its HMN / CPU button), on Fox, so its results window keeps the
        # fighter's name next to the consoles' names.
        door4 = a.stub.peek('mnCharSel_803F0DFC', 4 * 0x24)[3 * 0x24:4 * 0x24]
        left, right = struct.unpack('>ff', door4[0x14:0x1C])
        for _ in range(3):
            if a.css.doors()[3]['p_kind'] == 1:
                break
            steer_cursor(a, ((left + right) / 2, -2.2), lambda x, y: left + 0.5 < x < right - 0.5 and -4.0 < y < -0.4)
            a.stub.press(KEY_A, 4, 30)
        check('A makes door 4 a CPU', a.css.doors()[3]['p_kind'] == 1, str(a.css.doors()[3]))
        if a.css.doors()[3]['sel_icon'] >= 0x19:
            # the CPU's coin is not on a fighter: pick it up and put it on Fox
            _, tx, ty = token(a, 3)
            steer_cursor(a, (tx, ty), lambda x, y: abs(x - tx) < 1.0 and abs(y - ty) < 1.0)
            a.stub.press(KEY_A, 4, 30)
            take_fox(a, 3, cpu=True)
        check('the CPU on door 4 is on Fox', a.css.doors()[3]['sel_icon'] < 0x19, str(a.css.doors()[3]))
        a.stub.until_frames(90)
        a.capture('01-css')

        # ---- match 1: C vanishes during it, A quits ----
        start_match(1, (a, b, c3))
        snap = [l for l in a.log().splitlines() if 'Group: results names:' in l]
        print('names as the match began on A:', snap[-1:], flush=True)
        a.stub.until_frames(300)
        emu.stop(c3.proc)
        print('C stopped', flush=True)
        wait_log(a, 'Group: port 3 dropped', 'C\'s fighter becomes a CPU on A', 240)
        wait_log(b, 'Group: port 3 dropped', 'C\'s fighter becomes a CPU on B', 120)
        marks = {'A': len(a.log()), 'B': len(b.log())}
        quit_match(a)
        wait_log(a, 'Group: the results screen', 'A quits: its results screen', 240)
        a.stub.poke('mp_test_keys', 0)
        wait_log(b, 'Group: the results screen', 'B\'s results screen', 240)
        to_panels(a, marks['A'], a)
        to_panels(b, marks['B'], a)
        # the presses that took A past the wait may have made A ready: not yet (the leave and join below need the
        # screen to stay up)
        for _ in range(5):
            st = steps(a, marks['A'])
            if st and not st[-1][1] & 1:
                break
            a.stub.press(KEY_START, 4, 60)
        check('A is not ready yet', bool(steps(a, marks['A'])) and not steps(a, marks['A'])[-1][1] & 1,
              str(steps(a, marks['A'])[-3:]))
        for c in (a, b):
            got, named = labels(c.stub)
            print(f'{c.name} labels:', got, f'named {named:x}', flush=True)
            print(f'{c.name} widths:', struct.unpack('>4f', c.stub.peek('mp_res_drawn', 16)), flush=True)
            # SIS fitting: a line wider than its box is narrowed to it (the measured width is then the box's half, in
            # the renderer's units); a short one keeps its own width
            drawn = struct.unpack('>4f', c.stub.peek('mp_res_drawn', 16))
            width, size = struct.unpack('>ff', c.stub.peek('mp_res_width', 4) + c.stub.peek('mp_res_size', 4))
            fit = width / size / 2
            check(f'{c.name}: the widest name (WWWWWWWWWW) is narrowed into its box, a short one (Anna) is not',
                  abs(drawn[2] - fit) < 0.5 and drawn[0] < fit - 1, f'measured {drawn}, box half {fit}')
            check(f'{c.name}: the windows show Anna, Zoe Angstr and WWWWWWWWWW instead of the fighter (C dropped to a CPU '
                  'and keeps its name)', got == want and named == 0x7, f'{got} != {want}, named {named:x}')
        a.capture('02-results')
        b.capture('02-results')
        if args.tune:
            base = struct.unpack('>ff', a.stub.peek('mp_res_size', 4) + a.stub.peek('mp_res_dy', 4))
            for size, dy in ((0.05, -0.5), (0.05, -1.1)):
                put_float(a, 'mp_res_size', size)
                put_float(a, 'mp_res_dy', dy)
                a.stub.until_frames(10)
                a.capture(f'tune-{size}-{dy}')
            put_float(a, 'mp_res_size', base[0])
            put_float(a, 'mp_res_dy', base[1])

        # ---- B vanishes on the results screen; a PC console joins and takes a free port ----
        emu.stop(b.proc)
        print('B stopped', flush=True)
        # A gone console counts as ready on the results screen (its own log lines may come late: buffered)
        end = time.monotonic() + 240
        while time.monotonic() < end and not (steps(a, marks['A']) and steps(a, marks['A'])[-1][1] & 2):
            time.sleep(1)
        check('A notices that B left (B counts as ready)', bool(steps(a, marks['A'])) and steps(a, marks['A'])[-1][1] & 2,
              str(steps(a, marks['A'])[-3:]))
        a.stub.until_frames(60)
        got, named = labels(a.stub)
        check('B left: A\'s labels do not change', got == want and named == 0x7, f'{got}, named {named:x}')
        pc = out / 'pc'
        pc.mkdir()
        (pc / 'group.ini').write_text('\n'.join(['role=join', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=20000',
                                                 f'name={DEE}', 'test_games=1', 'test_frames=600', 'test_character=1',
                                                 '']) + '\n', encoding='utf-8')
        pc_log = open(out / 'pc.log', 'w', encoding='utf-8')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        wait_log('pc', 'group-test: in the lobby on port', 'the PC console Dee joins during the results', 120)
        line = [l for l in text_of('pc').splitlines() if 'group-test: in the lobby on port' in l][-1]
        dee_port = int(line.split('on port ')[1].split()[0])
        print('Dee is on port', dee_port, flush=True)
        check('Dee takes a port a player of this match played (the roster now names it Dee)', dee_port in (2, 3), line)
        a.stub.until_frames(120)
        got, named = labels(a.stub)
        check('Dee joined: A\'s labels do not change', got == want and named == 0x7, f'{got}, named {named:x}')
        a.capture('03-results-dee-joined')

        # A gets ready: everyone who played is (B and C left), the lobby reopens.
        for _ in range(10):
            if a.scene() == 8:
                break
            st = steps(a, marks['A'])
            if st and st[-1][0] == 3 and not st[-1][1] & 1:
                a.stub.press(KEY_START, 4, 20)
            time.sleep(5)
        wait_scene(a, 8, 'A is back on the character select', 180)
        a.stub.until_frames(120)
        a.capture('04-css-after')

        # ---- match 2: A and Dee (and the CPU C's door became) ----
        marks = {'A': len(a.log())}
        start_match(2, (a,))
        if not wait_log(a, 'Group: the results screen', 'match 2 ends (the PC console\'s checksums never match)', 120,
                        2, False):
            quit_match(a)
            wait_log(a, 'Group: the results screen', 'match 2 quit: A\'s results screen', 240, 2)
        a.stub.poke('mp_test_keys', 0)
        to_panels(a, marks['A'], a)
        got, named = labels(a.stub)
        want2 = [sis('Anna'), b'', b'', b'']
        want2[dee_port - 1] = sis(DEE)
        print('A labels, match 2:', got, f'named {named:x}', flush=True)
        check('match 2: A\'s results show Anna and Dee; the CPU keeps its fighter\'s name', got == want2 and
              named == (1 | 1 << (dee_port - 1)), f'{got} != {want2}, named {named:x}')
        a.capture('05-results-match2')
    finally:
        if pc_proc is not None and pc_proc.poll() is None:
            pc_proc.kill()
        for c in (a, b, c3):
            emu.stop(c.proc)
        time.sleep(1)
        for c in (a, b, c3):
            p = c.sd / 'game.log'
            if p.exists():
                shutil.copy2(p, out / f'game-{c.name}.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
