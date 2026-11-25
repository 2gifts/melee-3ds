"""The host's Stage Selection rule, Random from a joiner, VS Records in detail, and the host leaving while a console is
on its prize screen, with two emulated fresh-save consoles.

Both run the fresh-save development build with group.ini test_vs_matches=49; A also has test_nametag=6 (its player plays
with name tag id 5, as if chosen on the character select). A hosts a 1 minute time match, B joins, both take Fox.
  rules    B points at the Rules banner and presses A: refused with the "not allowed" sound (logged).
  Random   Stage Selection Any. B presses the Random icon: B sends a random pick, A's save picks the stage, and both
           consoles play that stage.
  records  A keeps walking off the stage, so B wins. On each console, from the log's before/after lines: the Fox record
           has 2 more matches (two Fox players), 1 more win and 1 more loss; A's tag 5 has more play time with Fox and
           no KOs credited against another tag (B has no tag of A's save); B's slot carries no tag on A (0x78) and A's
           none on B.
  Loser    Stage Selection Loser: A lost match 1, so port 1 (A) picks; B's pad does nothing on the stage select.
  leaving  Both saves at 99 VS matches; match 2 (B walks off); both reach their prize screens; A goes back to the
           character select and leaves the lobby (Back, "Yes") while B is still on its prize screen. B's prize screen
           ends normally, its character select sees the session gone and leaves, and B's earned challenger starts.

    docker/emu-test.sh run tools/group/group_stage_rules_test.py [--show]
Results: $MELEE_TEST_OUT/group-stage-rules/ (PNGs, both game.logs)."""
import argparse
import os
import re
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
from group_menu_test import CPAD_RIGHT, CPAD_UP, KEY_A, KEY_B, KEY_START, KEY_UP, to_main_menu  # noqa: E402
from group_results_test import CPAD_DOWN, CPAD_LEFT, FOX_TILE, RULES, Console, end_session_if_asked  # noqa: E402

LINK_PORT = 44180
THING = 0x1868                     # gmm_x0.thing (struct gmm_x1868; types.h says 0x1898, the build has 0x1868): unk_8.x0 and unk_28.x0 are the VS match counters
RANDOM_ICON = 29                   # mnStageSel_804D6CAE: the Random icon
RECORD = re.compile(r'p(\d) f(\d+) m(\d+) w(\d+) l(\d+) t(\d+) tp(\d+) tk(\d+);')


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
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-stage-rules'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    ini = 'test_vs_matches=49\ntimeout_ms=4000\n'
    a = Console('A', 24891, sym, out, args.dsx, args.show, 'name=Anna\ntest_nametag=6\n' + ini, 'group-stage-rules', LINK_PORT)
    time.sleep(3)
    b = Console('B', 24892, sym, out, args.dsx, args.show, 'name=Beeb\n' + ini, 'group-stage-rules', LINK_PORT)
    base = sym['gmMainLib_8045A6C0']

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
        wait_log(c, 'group: Group, looking for games', f'{c.name}: Group opens the Host / Join page', 60, n + 1)

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

    def cursor_to(c, port, tx, ty):
        for _ in range(400):
            _, x, y = c.css.cursor(port)
            k = toward(x, y, tx, ty)
            if not k:
                return True
            c.stub.press(k, 1, 1)
        return False

    def sss_cursor(c):
        ptr = struct.unpack('>I', c.stub.peek('mp_sss_cursor', 4))[0]
        return struct.unpack('>ff', c.stub.read(ptr + 0x38, 8)) if ptr else (0.0, 0.0)

    def to_random_icon(c):
        """Sweep the stage select cursor over its whole area until the Random icon is selected."""
        for ty in [y * 1.0 for y in range(18, -19, -3)]:
            for tx in [x * 1.0 for x in range(-26, 27, 3)]:
                for _ in range(200):
                    x, y = sss_cursor(c)
                    k = toward(x, y, tx, ty, 1.0)
                    if not k:
                        break
                    c.stub.press(k, 1, 1)
                if c.stub.peek('mnStageSel_804D6CAE', 1)[0] == RANDOM_ICON:
                    return True
        return False

    def press_start(c):
        n = c.log().count('GROUPCSS Start pressed')
        for _ in range(8):
            c.stub.press(KEY_START, 4, 30)
            if c.log().count('GROUPCSS Start pressed') > n:
                return True
        return False

    def pick_stage(c, n_picked):
        for _ in range(8):
            if a.log().count('group: starting a match') > n_picked:
                return
            c.stub.press(CPAD_UP, 6, 4)
            c.stub.press(KEY_A, 4, 60)

    def to_panels(c):
        def at_panels():                                     # this results screen only: earlier ones logged step 3 too
            return 'Group: results step 3' in c.log().rpartition('Group: the results screen')[2]
        end = time.monotonic() + 240
        while time.monotonic() < end and not at_panels():
            c.stub.press(KEY_A, 4, 30)
        c.stub.until_frames(60)
        c.stub.press(KEY_START, 4, 30)

    def records(c, which):
        lines = [l for l in c.log().splitlines() if f'Group: VS Records {which}:' in l]
        if not lines:
            return {}
        return {int(m[0]): tuple(int(v) for v in m[1:]) for m in RECORD.findall(lines[-1])}

    def blocks(c):
        return re.findall(r'Slippi online: match ready, stage (\d+)', c.log())

    def set_rule(stage_sel):
        a.stub.write(base + RULES + 7, bytes([stage_sel]))

    try:
        time.sleep(15)
        for c in (a, b):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))
        a.stub.write(base + RULES + 2, bytes([0, 1]))        # a 1 minute time match
        set_rule(0)                                          # Stage Selection: Any
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

        # ---- the Rules banner on a joiner: refused, with the "not allowed" sound ----
        check('B\'s cursor reaches the Rules banner', cursor_to(b, 1, 0.0, 24.0), str(b.css.cursor(1)))
        n = b.log().count('not allowed here')
        b.stub.press(KEY_A, 4, 30)
        wait_log(b, 'GROUPCSS the rules and the Teams switch are the host\'s: not allowed here',
                 'B presses A on the Rules banner: refused ("not allowed" sound)', 30, n + 1, False)
        check('B is still on the character select (no rules menu)', b.scene() == 8)
        cursor_to(b, 1, 0.0, 0.0)

        # ---- match 1: Any; B presses Random; A's save picks ----
        check('A presses Start', press_start(a))
        wait_log(b, 'Group: stage selection rule 0, picker 0', 'Any: every console picks (B)', 120)
        b.stub.until_frames(60)
        check('B\'s stage select cursor reaches the Random icon', to_random_icon(b),
              f'cursor {sss_cursor(b)}, icon {b.stub.peek("mnStageSel_804D6CAE", 1)[0]}')
        b.stub.press(KEY_A, 4, 30)
        wait_log(b, 'GROUPCSS stage select: Random; the host picks the stage', 'B sends a random pick', 60)
        wait_log(a, 'GROUPCSS stage select: a random pick; this save picks stage', 'A\'s save picks the stage', 60)
        picked = re.findall(r'this save picks stage (\d+)', a.log())
        wait_log(a, 'group: starting a match', 'the match starts on the stage A picked', 120)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'match 1 starts on {c.name}', 240)
        want = picked[-1] if picked else None
        check('both consoles play the stage A\'s save picked', blocks(a)[-1:] == [want] and blocks(b)[-1:] == [want],
              f'A picked {picked}, blocks A {blocks(a)} B {blocks(b)}')

        a.stub.until_frames(240)
        a.stub.poke('mp_test_keys', CPAD_LEFT)               # A keeps walking off: B wins
        for c in (a, b):
            wait_log(c, 'Group: the results screen', f'match 1 over: {c.name}\'s results screen', 900)
        a.stub.poke('mp_test_keys', 0)
        to_panels(a)
        to_panels(b)
        for c in (a, b):
            wait_log(c, 'Group: VS Records after', f'{c.name}: VS Records written', 120)
        ra0, ra1, rb0, rb1 = records(a, 'before'), records(a, 'after'), records(b, 'before'), records(b, 'after')
        print('records A', ra0, ra1, 'B', rb0, rb1, flush=True)
        for name, r0, r1 in (('A', ra0, ra1), ('B', rb0, rb1)):
            ok = 1 in r0 and 1 in r1 and 2 in r1
            check(f'{name}: the Fox record: 2 more matches, 1 more win, 1 more loss', ok and
                  r1[1][1] == r0[1][1] + 2 and r1[1][2] == r0[1][2] + 1 and r1[1][3] == r0[1][3] + 1, f'{r0} -> {r1}')
        check('A: its own slot plays with tag 5; B\'s slot has none on A\'s save (0x78)',
              ra1.get(1, (0,) * 7)[4] == 5 and ra1.get(2, (0,) * 7)[4] == 0x78, str(ra1))
        check('A: tag 5 gets play time with Fox and no KOs against a tag (B has none here)',
              ra1[1][5] > ra0[1][5] and ra1[1][6] == ra0[1][6], f'{ra0.get(1)} -> {ra1.get(1)}')
        check('B: neither slot has a tag of B\'s save (no test tag; A\'s tag is not B\'s)',
              rb1.get(1, (0,) * 7)[4] == 0x78 and rb1.get(2, (0,) * 7)[4] == 0x78, str(rb1))
        for c in (a, b):
            wait_scene(c, 8, f'{c.name} through its prize screen: back on the character select', 240, True, KEY_A)

        # ---- Loser: A lost match 1, so A picks ----
        set_rule(4)
        a.stub.until_frames(60)
        check('A presses Start (Loser)', press_start(a))
        wait_log(a, 'Group: stage selection rule 4, picker 1', 'Loser: port 1 (A, the loser of match 1) picks', 120)
        wait_log(b, 'Group: stage selection rule 4, picker 1', 'Loser: B sees port 1 pick', 120)
        b.stub.press(CPAD_UP, 6, 4)
        b.stub.press(KEY_A, 4, 30)
        check('B (not the picker) picks nothing', a.log().count('group: starting a match') == 1)

        # ---- match 2, then the host leaves while B is on its prize screen ----
        for c in (a, b):
            c.stub.write(base + THING + 0x08, struct.pack('>I', 99))
            c.stub.write(base + THING + 0x28, struct.pack('>I', 99))
            counters = (c.stub.read(base + THING + 0x08, 4), c.stub.read(base + THING + 0x28, 4))
            check(f'{c.name}: both VS match counters read back 99', counters == (struct.pack('>I', 99),) * 2,
                  f'read {counters[0].hex()} {counters[1].hex()}')
        pick_stage(a, 1)
        wait_log(a, 'group: starting a match', 'match 2: A picks and the host starts it', 120, 2)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'match 2 starts on {c.name}', 240, 2)
        b.stub.until_frames(240)
        b.stub.poke('mp_test_keys', CPAD_LEFT)               # B walks off this time
        for c in (a, b):
            wait_log(c, 'Group: the results screen', f'match 2 over: {c.name}\'s results screen', 900, 2)
        b.stub.poke('mp_test_keys', 0)
        to_panels(a)
        to_panels(b)
        for c in (a, b):
            wait_log(c, 'Group: the prize screen', f'match 2: {c.name}\'s prize screen (the 100th VS match)', 240, 2)
        wait_scene(a, 8, 'A back on the character select, B still on its prize screen', 240, True, KEY_A)
        a.stub.until_frames(60)
        a.stub.press(KEY_B, 2, 4)
        a.stub.poke('mp_test_keys', KEY_B)
        a.stub.until_frames(60)
        a.stub.poke('mp_test_keys', 0)
        a.stub.until_frames(30)
        end_session_if_asked(a)
        wait_log(a, 'group: left the lobby', 'A (the host) leaves the lobby', 120)
        b.stub.until_frames(240)
        b.capture('01-prize-host-gone')
        for _ in range(40):                                  # B goes through its prize screen now
            if 'Group: a new challenger approaches' in b.log():
                break
            b.stub.press(KEY_A, 4, 30)
        wait_log(b, 'Group: a new challenger approaches',
                 'B: past its prize screen, the session gone, its earned challenger starts', 120)
        b.stub.until_frames(240)
        b.capture('02-challenger')
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
