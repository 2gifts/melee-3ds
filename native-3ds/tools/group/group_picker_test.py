"""The stage picker leaves during the stage select: two emulated consoles, the host's Stage Selection rule Turns.

A (Anna) hosts and B (Beeb) joins; both take Fox. B presses Start, and the turn goes to B (if it went to A first, A goes
Back and B presses Start again). Only B has a cursor, labelled P2 (screenshots of both). Then B's emulator stops, with no
stage picked: B pressed Start and is the picker, and the stage select goes on all the same. A's console turns B's door
into a CPU and the pick passes to A: its cursor appears, labelled P1 (screenshot), and A's pick starts the match.

    docker/emu-test.sh run tools/group/group_picker_test.py [--show]
Results: $MELEE_TEST_OUT/group-picker/ (PNGs and both game.logs)."""
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
from emu_group import (CPAD_DOWN, CPAD_LEFT, CPAD_RIGHT, CPAD_UP, FOX_TILE, KEY_A, KEY_B, KEY_START, KEY_UP,  # noqa: E402
                       MENU_VS, RULES, SCENE_CHOOSE, Console, to_main_menu)

LINK_PORT = 44250
STAGE_SEL = 7                           # GameRules.stage_sel
TURNS = 3                               # StageSelectMode: Turns (gm_80167FC4's case 3)
PICKER = re.compile(r'Group: stage selection rule (\d+), picker (\d+)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-picker'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    # timeout_ms: B's leave is noticed in seconds (the real default is 2000; a console polled through GDB runs slowly)
    a = Console('A', 24911, sym, out, args.dsx, args.show, 'name=Anna\ntimeout_ms=8000\n', 'group-picker', LINK_PORT)
    time.sleep(3)
    b = Console('B', 24912, sym, out, args.dsx, args.show, 'name=Beeb\ntimeout_ms=8000\n', 'group-picker', LINK_PORT)
    b_stopped = False

    def check(name, ok, detail=''):
        results.append((name, bool(ok)))
        print(('PASS ' if ok else 'FAIL ') + name, ('' if ok else detail) if detail else '', flush=True)
        return ok

    def wait_for(pred, what, timeout=120, fatal=False, detail=lambda: ''):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                if pred():
                    return check(what, True)
            except Exception:  # noqa: BLE001 (the stub reconnects)
                pass
            time.sleep(0.5)
        check(what, False, detail())
        if fatal:
            raise SystemExit(1)
        return False

    def wait_log(c, text, what, timeout=180, count=1, fatal=False):
        return wait_for(lambda: c.log().count(text) >= count, what, timeout, fatal,
                        lambda: f'console {c.name}: not in game.log ({count}x): {text}')

    def wait_scene(c, scene, what, timeout=180, fatal=False):
        return wait_for(lambda: c.scene() == scene, what, timeout, fatal, lambda: f'console {c.name}: scene {c.scene()}')

    def to_group_page(c):
        c.stub.goto_vs_menu()
        for _ in range(8):
            if c.stub.menu()[1] == 0:
                break
            c.stub.press(KEY_UP)
        n = c.log().count('group: Group, looking for games')
        c.stub.press(KEY_A)
        wait_log(c, 'group: the Solo/Group page is up', f'{c.name}: Melee: the Solo / Group submenu', 60, 1, True)
        c.stub.choose()
        wait_log(c, 'group: Group, looking for games', f'{c.name}: Group opens the list', 60, n + 1, True)
        wait_scene(c, SCENE_CHOOSE, f'{c.name}: "Choose a game."', 120, True)

    def token(c, door):
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BD0', 16)[4 * door:4 * door + 4])[0]
        return struct.unpack('>ff', c.stub.read(ptr, 0x10)[8:16])

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
                keys = 0
                if abs(x - cx) > 0.6:
                    keys |= CPAD_RIGHT if x < cx else CPAD_LEFT
                if abs(y - cy) > 0.6:
                    keys |= CPAD_UP if y < cy else CPAD_DOWN
                c.stub.press(keys, 1, 1)
            c.stub.press(KEY_A, 4, 30)
        return check(f'{c.name}: the token is down on Fox', placed())

    def pickers(c):
        return [(int(r), int(p)) for r, p in PICKER.findall(c.log())]

    def start_stage_select(c):
        """c presses Start on the character select; returns the picker (port + 1) both consoles were told."""
        n = len(pickers(a))
        for _ in range(8):
            c.stub.press(KEY_START, 4, 30)
            if len(pickers(a)) > n:
                break
        wait_for(lambda: len(pickers(a)) > n and len(pickers(b)) > n, f'{c.name}\'s Start: the stage select on both',
                 180, True)
        rule, picker = pickers(a)[-1]
        check(f'the host\'s rule is Turns ({rule}) and both agree on the picker ({picker})',
              rule == TURNS and pickers(b)[-1] == (rule, picker))
        return picker

    try:
        time.sleep(15)
        for c in (a, b):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))
        a.stub.write(sym['gmMainLib_8045A6C0'] + RULES + STAGE_SEL, bytes([TURNS]))   # the host's Stage Selection

        to_group_page(a)
        a.touch(118, 50)                 # HOST
        wait_scene(a, 8, 'A hosts: the lobby character select', 180, True)
        to_group_page(b)
        b.stub.until_frames(120)
        b.touch(160, 80)                 # the first game in the list
        wait_scene(b, 8, 'B joins A\'s lobby', 120, True)
        take_fox(a, 0)
        take_fox(b, 1)
        for c in (a, b):
            c.stub.until_frames(60)

        picker = start_stage_select(b)
        if picker == 1:                  # A's turn first: A goes Back, and the next turn is B's
            a.stub.press(KEY_B, 4, 60)
            wait_for(lambda: a.scene() == 8 and b.scene() == 8, 'A (the picker) goes Back: both on the character select',
                     120, True)
            for c in (a, b):
                c.stub.until_frames(60)
            picker = start_stage_select(b)
        check('B pressed Start and it is B\'s turn: B picks (P2)', picker == 2, f'picker {picker}')
        for c in (a, b):
            c.stub.until_frames(90)
        b.capture('1-b-picks')
        a.capture('1-a-waits')

        emu.stop(b.proc)                 # B leaves before picking: it pressed Start, and it is the picker
        b_stopped = True
        print('stopped B', flush=True)
        wait_log(a, 'GROUPCSS stage select: port 1 picks the stage now', 'the pick passes to A (port 1)', 120, 1, True)
        check('the stage select goes on on A (not back to the character select)', a.scene() not in (8, SCENE_CHOOSE),
              f'scene {a.scene()}')
        a.stub.until_frames(90)
        a.capture('2-a-picks')
        for _ in range(8):               # the cursor starts below the grid: up onto a stage, then A
            if 'group: starting a match' in a.log():
                break
            a.stub.press(CPAD_UP, 6, 4)
            a.stub.press(KEY_A, 4, 60)
        wait_log(a, 'group: starting a match', 'A picks a stage: the match starts', 120)
        wait_log(a, 'Slippi online: match starts', 'the match runs on A', 240)
        check('A did not crash', a.proc.poll() is None and 'PANIC' not in a.log())
    finally:
        for c, stopped in ((a, False), (b, b_stopped)):
            if not stopped:
                emu.stop(c.proc)
        time.sleep(1)
        for c in (a, b):
            if (c.sd / 'game.log').exists():
                shutil.copy2(c.sd / 'game.log', out / f'game-{c.name}.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
