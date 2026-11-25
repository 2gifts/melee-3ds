"""A session lost during a match, with two emulated consoles: the joiner lands on "Choose a game." with a notice.

A hosts (Melee -> Group -> Host), B joins, both take Fox and A starts a match. A few seconds into it A's emulator is
stopped (the host vanishes, no goodbye). B's link times out (group.ini timeout_ms=4000), its match ends, and B comes back
through the character select to "Choose a game." (the tournament setup scene, tools/group_edits/group_lobby_screen.py):
a top-screen dialog says why ("The host stopped responding", one OK) and the Group list is open under it. OK closes the
dialog and the list stays.

    docker/emu-test.sh run tools/group/group_lost_test.py [--show]
Results: $MELEE_TEST_OUT/group-lost/ (PNGs, both game.logs)."""
import argparse
import os
import shutil
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'slippi'))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from group_menu_test import CPAD_RIGHT, CPAD_UP, KEY_A, KEY_START, KEY_UP, SCENE_CHOOSE, to_main_menu  # noqa: E402
from group_results_test import CPAD_DOWN, CPAD_LEFT, FOX_TILE, Console  # noqa: E402

LINK_PORT = 44190


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-lost'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    ini = 'timeout_ms=4000\n'
    a = Console('A', 24893, sym, out, args.dsx, args.show, 'name=Anna\n' + ini, 'group-lost', LINK_PORT)
    time.sleep(3)
    b = Console('B', 24894, sym, out, args.dsx, args.show, 'name=Beeb\n' + ini, 'group-lost', LINK_PORT)

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

    def wait_scene(c, scene, what, timeout=180, fatal=True, press=0):
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
            if press:
                c.stub.press(press, 4, 30)
            else:
                time.sleep(0.5)
        check(what, False, f'console {c.name}: scene {s}')
        if fatal:
            raise SystemExit(1)
        return False

    def to_group_page(c):
        c.stub.goto_vs_menu()
        for _ in range(8):
            if c.stub.menu()[1] == 0:
                break
            c.stub.press(KEY_UP)
        n = c.log().count('group: the Solo/Group page is up')
        c.stub.press(KEY_A)
        wait_log(c, 'group: the Solo/Group page is up', f'{c.name}: the Solo/Group page opens', 60, n + 1)
        n = c.log().count('group: Group, looking for games')
        c.stub.choose()
        wait_log(c, 'group: Group, looking for games', f'{c.name}: Group opens the list', 60, n + 1)
        wait_scene(c, SCENE_CHOOSE, f'{c.name}: on "Choose a game."', 120)

    def token(c, door):
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BD0', 16)[4 * door:4 * door + 4])[0]
        bts = c.stub.read(ptr, 0x10)
        x, y = struct.unpack('>ff', bts[8:16])
        return x, y

    def toward(x, y, tx, ty, tol=0.6):
        k = 0
        if abs(x - tx) > tol:
            k |= CPAD_RIGHT if x < tx else CPAD_LEFT
        if abs(y - ty) > tol:
            k |= CPAD_UP if y < ty else CPAD_DOWN
        return k

    def take_fox(c, door):
        left, right, bottom, top = FOX_TILE
        cx, cy = (left + right) / 2, (bottom + top) / 2

        def placed():
            return c.css.cursor(door)[0] != 1 and c.css.doors()[door]['sel_icon'] < 0x19
        for _ in range(4):
            if placed():
                break
            for _ in range(300):
                x, y = token(c, door)
                if left + 0.8 < x < right - 0.8 and bottom + 0.8 < y < top - 0.8:
                    break
                c.stub.press(toward(x, y, cx, cy), 1, 1)
            c.stub.press(KEY_A, 4, 30)
        return check(f'{c.name}: the token is down on Fox', placed())

    def press_start(c):
        n = c.log().count('GROUPCSS Start pressed')
        for _ in range(8):
            c.stub.press(KEY_START, 4, 30)
            if c.log().count('GROUPCSS Start pressed') > n:
                return True
        return False

    try:
        time.sleep(15)
        for c in (a, b):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))
        to_group_page(a)
        a.touch(118, 50)
        wait_scene(a, 8, 'A hosts: the lobby character select')
        to_group_page(b)
        b.stub.until_frames(120)
        b.touch(160, 80)
        wait_scene(b, 8, 'B joins A\'s lobby')
        b.stub.until_frames(120)
        take_fox(a, 0)
        take_fox(b, 1)

        # ---- a match, and the host vanishes during it ----
        check('A presses Start', press_start(a))
        for _ in range(8):                                   # A picks a stage
            if 'group: starting a match' in a.log():
                break
            a.stub.press(CPAD_UP, 6, 4)
            a.stub.press(KEY_A, 4, 60)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'the match starts on {c.name}', 240)
        b.stub.until_frames(240)
        b.capture('01-match')
        lists = b.log().count('group: Group, looking for games')
        notes = b.log().count('group: dialog "')
        emu.stop(a.proc)                                     # the host vanishes, no goodbye
        check('A (the host) is gone', a.proc.poll() is not None)

        # ---- B: the match ends, then "Choose a game." with the notice and the list ----
        wait_log(b, 'group: left the lobby', 'B leaves the lost session', 600, 1, False)
        wait_scene(b, SCENE_CHOOSE, 'B lands on "Choose a game."', 240, False, KEY_START)
        wait_log(b, 'group: dialog "', 'B: a notice says why the session ended', 60, notes + 1, False)
        note = [l for l in b.log().splitlines() if 'group: dialog "' in l][-1:]
        check('the notice: the host stopped responding (or left), one OK button',
              bool(note) and 'The host' in note[0] and note[0].endswith('[OK]'), str(note))
        wait_log(b, 'group: Group, looking for games', 'B: the Group list is open under the notice', 60, lists + 1,
                 False)
        b.stub.until_frames(40)
        b.capture('02-notice')
        b.stub.press(KEY_A, 2, 30)                           # OK
        b.stub.until_frames(40)
        b.capture('03-after-ok')
        check('OK: still on "Choose a game.", the list open',
              b.scene() == SCENE_CHOOSE and b.log().count('group: Group, looking for games') == lists + 1,
              f'scene {b.scene()}')
        check('B did not crash', b.proc.poll() is None and 'PANIC' not in b.log())
    finally:
        for c in (a, b):
            emu.stop(c.proc)
        time.sleep(1)
        for c in (a, b):
            if (c.sd / 'game.log').exists():
                shutil.copy2(c.sd / 'game.log', out / f'game-{c.name}.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
