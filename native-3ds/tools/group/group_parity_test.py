"""Group play as local Versus on one console, with two emulated fresh-save consoles: the save after a group match and
the host's Stage Selection rule.

Both consoles run the fresh-save development build with group.ini test_vs_matches=49 (each save one VS match short of
a stage unlock and of an earned challenger). A hosts with Stage Selection Turns and a 1 minute time match; B joins; both take
Fox.
  match 1  Turns: port 1 (A) picks; B's stage select has no picker of its own (its log says port 1 picks). In a 1
           minute time match A keeps walking off the stage, so B wins. Then on both consoles: VS Records written, the save checks find a notice (the stage
           unlocked by the 50th VS match) and the prize screen follows the results. A goes through its prize screen
           first: on A's bottom screen B's row says VIEWING A PRIZE and A's Start is refused; once B is through, both
           are back on the character select.
  Start 2  Turns: now port 2 (B) picks, and A's stage select follows; then B vanishes: the pick passes to port 1 (A),
           whose stage select takes it ("port 1 picks the stage now") and goes back to the character select with B.
  leaving  A leaves the lobby (Back, "End it"): its save has earned a fighter, so "A new challenger approaches" starts
           (the GM_CHALLENGER_APPROACH mode) instead of the menus.

    docker/emu-test.sh run tools/group/group_parity_test.py [--show]
Results: $MELEE_TEST_OUT/group-parity/ (PNGs, both game.logs)."""
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
from group_menu_test import CPAD_RIGHT, CPAD_UP, KEY_A, KEY_B, KEY_START, KEY_UP, MENU_VS, to_main_menu  # noqa: E402
from group_results_test import CPAD_DOWN, CPAD_LEFT, FOX_TILE, RULES, Console, DIALOG_YES  # noqa: E402

LINK_PORT = 44170
FIGHTERS, FIGHTERS_LEN = 0x1898 + 0x6C4, 0x1760 - 0x6C4   # gmm_x0.thing.x1F2C: every fighter's VS Records


def default(name):
    d = os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx')
    e = os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf')
    return d.replace('melee-development.3dsx', 'melee-fresh-development.3dsx') if name == 'dsx' else \
        e.replace('build/game-opt/', 'build/game-opt-fresh/')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=default('dsx'))
    ap.add_argument('--elf', default=default('elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-parity'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    ini = 'test_vs_matches=49\ntimeout_ms=3000\n'
    a = Console('A', 24881, sym, out, args.dsx, args.show, 'name=Anna\n' + ini, 'group-parity', LINK_PORT)
    time.sleep(3)
    b = Console('B', 24882, sym, out, args.dsx, args.show, 'name=Beeb\n' + ini, 'group-parity', LINK_PORT)

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

    def strip(c, port):
        raw = c.stub.peek('mp_group_strip_text', 96)[24 * port:24 * port + 24]
        return raw.split(b'\0')[0].decode(errors='replace')

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
        c.touch(238, 104)
        wait_log(c, 'group: Group, looking for games', f'{c.name}: Group opens the Host / Join page', 60, n + 1)

    def token(c, door):
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BD0', 16)[4 * door:4 * door + 4])[0]
        bts = c.stub.read(ptr, 0x10)
        x, y = struct.unpack('>ff', bts[8:16])
        return bts[5], x, y

    def take_fox(c, door):
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
        return check(f'{c.name}: the token is down on Fox', placed())

    def press_start(c):
        n = c.log().count('GROUPCSS Start pressed')
        for _ in range(8):
            c.stub.press(KEY_START, 4, 30)
            if c.log().count('GROUPCSS Start pressed') > n:
                return True
        return False

    def pick_stage(c, n_picked):
        for _ in range(8):
            if c.log().count('group: starting a match') > n_picked or a.log().count('group: starting a match') > n_picked:
                return
            c.stub.press(CPAD_UP, 6, 4)
            c.stub.press(KEY_A, 4, 60)

    def to_panels(c):
        """Press through the results screen's intro to the players' panels (step 3), then Start once: ready."""
        end = time.monotonic() + 240
        while time.monotonic() < end and 'Group: results step 3' not in c.log():
            c.stub.press(KEY_A, 4, 30)
        c.stub.until_frames(60)
        c.stub.press(KEY_START, 4, 30)

    try:
        time.sleep(15)
        for c in (a, b):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))
        base = sym['gmMainLib_8045A6C0']
        a.stub.write(base + RULES + 2, bytes([0, 1]))        # a 1 minute time match (A's self-destructs decide it)
        a.stub.write(base + RULES + 7, bytes([3]))           # Stage Selection: Turns
        records = {c.name: c.save(FIGHTERS, FIGHTERS_LEN) for c in (a, b)}

        to_group_page(a)
        a.touch(118, 50)                                     # HOST
        wait_scene(a, 8, 'A hosts: the lobby character select')
        wait_log(a, 'test_vs_matches: this save\'s VS match counters are now 49', 'A: the save is one match short', 30)
        to_group_page(b)
        b.stub.until_frames(120)
        b.touch(160, 80)
        wait_scene(b, 8, 'B joins A\'s lobby')
        b.stub.until_frames(120)
        take_fox(a, 0)
        take_fox(b, 1)
        for c in (a, b):
            c.stub.until_frames(90)

        # ---- match 1: Turns, port 1 picks ----
        check('A presses Start', press_start(a))
        wait_log(a, 'Group: stage selection rule 3, picker 1', 'Turns: port 1 (A) picks the first stage (A)', 120)
        wait_log(b, 'Group: stage selection rule 3, picker 1', 'Turns: port 1 picks, as B sees it', 120)
        b.stub.until_frames(30)
        b.capture('01-sss-not-picker')
        a.capture('01-sss-picker')
        nb = b.log().count('GROUPCSS stage select: stage')
        b.stub.press(CPAD_UP, 6, 4)                          # B's pad does nothing on A's pick
        b.stub.press(KEY_A, 4, 30)
        b.stub.press(KEY_B, 4, 30)
        check('B (not the picker) cannot pick or go back', a.log().count('group: starting a match') == 0 and
              b.scene() != 8)
        pick_stage(a, 0)
        wait_log(a, 'group: starting a match', 'match 1: A picks and the host starts it', 120)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'match 1 starts on {c.name}', 240)
        a.stub.until_frames(240)
        a.stub.poke('mp_test_keys', CPAD_LEFT)               # A walks off the stage, again and again
        for c in (a, b):
            wait_log(c, 'Group: the results screen', f'match 1 over: {c.name}\'s results screen', 900)
        a.stub.poke('mp_test_keys', 0)
        for c in (a, b):
            wait_log(c, 'Group: this save earned a notice or trophy', f'{c.name}: the save checks find a notice', 30)
        a.capture('02-results')

        # ---- the prize screen: A goes through first, B stays on it ----
        to_panels(a)
        to_panels(b)
        for c in (a, b):
            wait_log(c, 'Group: the prize screen', f'{c.name}: the prize screen follows the results', 60)
            wait_log(c, 'Group: VS Records written', f'{c.name}: VS Records written', 10)
            check(f'{c.name}: the fighters\' VS Records changed in the save', c.save(FIGHTERS, FIGHTERS_LEN) != records[c.name])
        b.stub.until_frames(60)
        b.capture('03-prize')
        wait_scene(a, 8, 'A through its prize screen: back on the character select', 180, True, KEY_A)
        a.stub.until_frames(120)
        a.capture('04-css-b-on-prize')
        check('A\'s bottom screen: B is VIEWING A PRIZE', strip(a, 1) == 'VIEWING A PRIZE', repr(strip(a, 1)))
        n_rule = a.log().count('Group: stage selection rule')
        press_start(a)
        a.stub.until_frames(120)
        check('A\'s Start is refused while B is on its prize screen', a.log().count('Group: stage selection rule') == n_rule
              and a.scene() == 8)
        wait_scene(b, 8, 'B through its prize screen: back on the character select', 180, True, KEY_A)
        b.stub.until_frames(120)
        check('A\'s bottom screen: B is no longer busy', strip(a, 1) != 'VIEWING A PRIZE', repr(strip(a, 1)))

        # ---- Start 2: Turns, port 2 picks; then B vanishes and the pick passes to port 1 ----
        check('A presses Start again', press_start(a))
        wait_log(a, 'Group: stage selection rule 3, picker 2', 'Turns: port 2 (B) picks the second stage', 120)
        wait_log(b, 'Group: stage selection rule 3, picker 2', 'B is the picker', 120)
        a.stub.until_frames(30)
        check('A\'s bottom screen: B PICKS THE STAGE', strip(a, 1) == 'PICKS THE STAGE', repr(strip(a, 1)))
        a.capture('05-sss-b-picks')
        emu.stop(b.proc)                                     # B vanishes during its pick
        wait_log(a, 'GROUPCSS stage select: port 1 picks the stage now', 'B gone: the pick passes to port 1 (A)', 60)
        a.stub.until_frames(60)
        a.capture('06-sss-pick-passed')
        wait_scene(a, 8, 'A, the picker now, goes back to the character select', 120, True, KEY_B)

        # ---- A leaves the lobby: the challenger its save earned ----
        a.stub.press(KEY_B, 2, 4)
        a.stub.poke('mp_test_keys', KEY_B)
        a.stub.until_frames(60)
        a.stub.poke('mp_test_keys', 0)
        a.stub.until_frames(30)
        a.touch(*DIALOG_YES)                                 # "End the session?" -> END IT (if asked)
        wait_log(a, 'Group: a new challenger approaches', 'A leaves: "A new challenger approaches" (its earned fighter)', 120)
        a.stub.until_frames(240)
        a.capture('07-challenger')
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
