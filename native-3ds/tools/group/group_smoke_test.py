"""Group play end to end in one session of two emulated consoles (and a PC console): the glue between the scenes and
the rules that tools/test_group.py and the PC consoles (run_pc_suite.py) check on their own.

A (Anna) and B (Beeb) boot to the main menu. Then:
  1. A: VS -> Melee: the top-screen Solo / Group submenu, its art filled; Group: "Choose a game." with the list open;
     HOME and sleep off; a refused HOME press (poked through the GDB stub) shows the notice.
  2. A hosts (a 1 minute time match, handicap Auto in its save), B joins. A turns door 3 into a CPU. B holds that door's
     CPU level slider (its CSS's own hold byte, set through the GDB stub): B reports the door in use and A's click on
     its Human / CPU / closed switch does nothing; let go, the same click closes it.
  3. B's C-stick turns A's camera (the relay), and back at rest.
  4. Both take Fox and A starts: nobody moves, the match is tied, a sudden death follows on both; on the results both
     screens show Anna and Beeb under the windows; both press ready and are back on the character select. Each saved
     its VS Records.
  5. A goes Back: "End the session?" (B's strip says the host is answering a prompt); "Yes": A lands on "Choose a
     game." (the list open), B there with a notice saying the host left.
  6. A PC console hosts a long match against a CPU; B joins it while it runs: the bottom screen's "Match in progress"
     page (screenshot), and the character select sends nothing. B leaves (Back held).
  7. A: the soft reset (L+R+Start) on "Choose a game.": the main menu, the session closed, HOME and sleep on.

    docker/emu-test.sh run tools/group/group_smoke_test.py [--show]
Results: $MELEE_TEST_OUT/group-smoke/ (PNGs, both game.logs, the PC console's log)."""
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
from emu_group import (CPAD_DOWN, CPAD_LEFT, CPAD_RIGHT, CPAD_UP, FOX_TILE, KEY_A, KEY_B, KEY_L, KEY_LEFT, KEY_R,  # noqa: E402
                       KEY_START, KEY_UP, MENU_VS, RULES, SCENE_CHOOSE, Console, dialog_up, to_main_menu)
from names_probe import labels, sis  # noqa: E402

LINK_PORT = 44230
TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
CSTICK_UP = 1 << 26                     # libctru KEY_CSTICK_UP: the dev build's test keys tilt the C-stick fully
DOOR_SIZE = 0x24                        # CSSDoor (css_probe.DOOR_SIZE)
HOLD_CPU = 0x12                         # CSSDoor.is_hold_cpu_slider
CPU_DOOR = 2                            # door 3
CPU, CLOSED = 1, 3                      # the scene's p_kind


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-smoke'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    sym = emu.elf_symbols(args.elf)
    results = []
    t0 = time.monotonic()
    a = Console('A', 24901, sym, out, args.dsx, args.show, 'name=Anna\n', 'group-smoke', LINK_PORT)
    time.sleep(3)
    b = Console('B', 24902, sym, out, args.dsx, args.show, 'name=Beeb\n', 'group-smoke', LINK_PORT)
    pc_proc = None

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
        check(f'{c.name}: Melee is hovered on the VS menu', c.stub.menu() == (MENU_VS, 0), str(c.stub.menu()))
        n = c.log().count('group: the Solo/Group page is up')
        c.stub.press(KEY_A)
        wait_log(c, 'group: the Solo/Group page is up', f'{c.name}: Melee: the Solo / Group submenu', 60, n + 1, True)
        n = c.log().count('group: Group, looking for games')
        c.stub.choose()
        wait_log(c, 'group: Group, looking for games', f'{c.name}: Group opens the list', 60, n + 1, True)
        wait_scene(c, SCENE_CHOOSE, f'{c.name}: "Choose a game."', 120, True)

    def door_addr(c, d, field):
        return c.stub.sym['mnCharSel_803F0DFC'] + d * DOOR_SIZE + field

    def cursor_on_switch(c, me, door):
        """Put c's cursor on door's Human / CPU / closed switch and press A there (as group_door_test)."""
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BC0', 16)[4 * me:4 * me + 4])[0]
        left, right = struct.unpack('>ff', c.stub.read(door_addr(c, door, 0x14), 8))
        x = (left + right) / 2
        for _ in range(3):               # the cursor is placed again each frame we hold it there
            c.stub.write(ptr + 0xC, struct.pack('>ff', x, -2.2))
            c.stub.until_frames(1)
        c.stub.poke('mp_test_keys', KEY_A)
        c.stub.write(ptr + 0xC, struct.pack('>ff', x, -2.2))
        c.stub.until_frames(2)
        c.stub.poke('mp_test_keys', 0)
        c.stub.until_frames(30)

    def token(c, door):
        ptr = struct.unpack('>I', c.stub.peek('mnCharSel_804A0BD0', 16)[4 * door:4 * door + 4])[0]
        x, y = struct.unpack('>ff', c.stub.read(ptr, 0x10)[8:16])
        return x, y

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

    def eye(c, frames=40):
        c.stub.until_frames(frames)
        return struct.unpack('>fff', c.stub.peek('mp_test_camera_eye', 12))

    def dist(p, q):
        return sum((x - y) ** 2 for x, y in zip(p, q)) ** 0.5

    def strip_text(c, port):
        """The bottom-screen strip's status text on port's row, as drawn (bottom_draw.c)."""
        return c.stub.peek('mp_group_strip_text', 96)[24 * port:24 * port + 24].split(b'\0')[0].decode(errors='replace')

    def results_steps(c, since):
        found = []
        for line in c.log()[since:].splitlines():
            if 'Group: results step ' in line:
                w = line.split('Group: results step ')[1].replace(',', '').split()
                found.append((int(w[0]), int(w[2], 16)))
        return found

    try:
        time.sleep(15)
        for c in (a, b):
            c.stub.poke('mp_test_frame_limit', 0)
            check(f'{c.name}: reach the main menu', to_main_menu(c.stub))
        check('A: the menu art was filled when the menus loaded', 'group: menu art filled' in a.log())
        # the host's rules: a 1 minute time match, handicap Auto (its save, as its Rules menu would set them)
        a.stub.write(sym['gmMainLib_8045A6C0'] + RULES + 2, bytes([0, 1]))
        a.stub.write(sym['gmMainLib_8045A6C0'] + RULES + 5, bytes([1]))

        # ---- 1. the submenu, "Choose a game.", HOME and sleep ----
        to_group_page(a)
        wait_log(a, 'group: HOME and sleep off (wireless session)', 'A: HOME and sleep off while the list is open', 60)
        a.stub.poke('group_test_home_reject', 1)
        wait_log(a, 'group: HOME pressed during wireless play', 'A: a refused HOME press shows the notice', 60)
        a.stub.until_frames(10)
        a.capture('01-choose-a-game-home-notice')

        # ---- 2. the lobby; a door in use on the other console ----
        a.touch(118, 50)                 # HOST
        wait_scene(a, 8, 'A hosts: the lobby character select', 180, True)
        to_group_page(b)
        b.stub.until_frames(120)
        b.touch(160, 80)                 # the first game in the list
        wait_scene(b, 8, 'B joins A\'s lobby', 120, True)
        wait_log(b, "GROUPCSS the host's rules: mode 0 time 1", 'B shows the host\'s rules (a 1 minute time match)', 60)
        b.stub.until_frames(120)
        cursor_on_switch(a, 0, CPU_DOOR)  # door 3: closed -> CPU (no console there, so not Human)
        wait_for(lambda: b.css.doors()[CPU_DOOR]['p_kind'] == CPU and a.css.doors()[CPU_DOOR]['p_kind'] == CPU,
                 'A makes door 3 a CPU, on both consoles', 30)
        b.stub.write(door_addr(b, CPU_DOOR, HOLD_CPU), b'\x01')   # B holds door 3's CPU level slider
        wait_log(b, f'GROUPCSS busy 1 why 1 doors {1 << CPU_DOOR:x}', 'B reports door 3 in use (its slider)', 60)
        b.stub.until_frames(60)
        cursor_on_switch(a, 0, CPU_DOOR)
        check('A\'s click on door 3\'s switch does nothing while B holds its slider',
              a.css.doors()[CPU_DOOR]['p_kind'] == CPU and b.css.doors()[CPU_DOOR]['p_kind'] == CPU)
        a.capture('02-door-in-use')
        b.stub.write(door_addr(b, CPU_DOOR, HOLD_CPU), b'\x00')
        wait_log(b, 'GROUPCSS busy 0 why 0 doors 0', 'B lets go of the slider', 60)
        b.stub.until_frames(60)
        cursor_on_switch(a, 0, CPU_DOOR)
        wait_for(lambda: a.css.doors()[CPU_DOOR]['p_kind'] == CLOSED and b.css.doors()[CPU_DOOR]['p_kind'] == CLOSED,
                 'let go: the same click closes door 3, on both consoles', 30)

        # ---- 3. the camera follows the other console's C-stick ----
        rest = eye(a)
        b.stub.poke('mp_test_keys', CSTICK_UP)
        turned = eye(a)
        b.stub.poke('mp_test_keys', 0)
        check('B\'s C-stick turns A\'s camera', dist(turned, rest) > 1.0, f'rest {rest}, turned {turned}')
        back = eye(a, 60)
        check('B\'s stick back to neutral: A\'s camera at rest', dist(back, rest) < 0.01, f'rest {rest}, now {back}')

        # ---- 4. a tied match, the sudden death, the results, the saves ----
        take_fox(a, 0)
        take_fox(b, 1)
        for c in (a, b):
            c.stub.until_frames(60)
        asked = a.log().count('GROUPCSS Start pressed')
        for _ in range(8):
            a.stub.press(KEY_START, 4, 30)
            if a.log().count('GROUPCSS Start pressed') > asked:
                break
        wait_log(a, 'GROUPCSS Start pressed', 'A\'s Start asks the lobby', 60, asked + 1)
        wait_for(lambda: a.scene() != 8, 'A: the stage select', 180)
        for _ in range(8):               # the stage cursor starts below the grid: up onto a stage, then A
            if 'group: starting a match' in a.log():
                break
            a.stub.press(CPAD_UP, 6, 4)
            a.stub.press(KEY_A, 4, 60)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'the match starts on {c.name}', 240, 1, True)
        wait_log(a, 'Group: the match was tied', 'nobody moved: the match is tied (A)', 400, 1, True)
        wait_log(b, 'Group: the match was tied', 'the match is tied (B)', 60)
        for c in (a, b):
            wait_log(c, 'Slippi online: match starts', f'the sudden death starts on {c.name}', 240, 2, True)
        wait_for(lambda: any(s == 2 for s, _ in results_steps(a, 0)), 'A: the results screen waits for a press', 600, True)
        a.stub.press(KEY_A, 4, 20)       # the press goes to the host and moves both screens on
        for c in (a, b):
            wait_for(lambda c=c: any(s == 3 for s, _ in results_steps(c, 0)), f'{c.name}: the results\' panels', 180, True)
        for c in (a, b):
            c.stub.until_frames(120)     # the panels come in
        a.capture('03-results')
        want = [sis('Anna'), sis('Beeb'), b'', b'']
        for c in (a, b):
            got, named = labels(c.stub)
            check(f'{c.name}: the results show Anna and Beeb under the windows', got == want and named == 0x3,
                  f'{got}, named {named:x}')
        for c in (a, b):
            c.stub.press(KEY_START, 4, 20)
        for c in (a, b):
            wait_scene(c, 8, f'both ready: {c.name} is back on the character select', 180)
            check(f'{c.name}: its VS Records were saved after the match',
                  'Group: VS Records written for this match' in c.log())

        # ---- 5. the host ends the session ----
        notes = b.log().count('group: dialog "')
        lists = {c.name: c.log().count('group: Group, looking for games') for c in (a, b)}
        a.stub.press(KEY_B, 2, 4)
        a.stub.press(KEY_B, 45, 4)       # Back held
        wait_for(lambda: dialog_up(a), 'A\'s Back asks "End the session?"', 30)
        wait_for(lambda: strip_text(b, 0) == 'ANSWERING A PROMPT', 'B\'s strip: the host is answering a prompt', 30,
                 detail=lambda: repr(strip_text(b, 0)))
        a.capture('04-end-the-session')
        a.stub.press(KEY_LEFT, 2, 10)    # Yes
        a.stub.press(KEY_A, 2, 20)
        wait_scene(a, SCENE_CHOOSE, 'Yes: A lands on "Choose a game."', 180)
        wait_log(a, 'group: Group, looking for games', 'A: the list open again', 60, lists['A'] + 1)
        wait_scene(b, SCENE_CHOOSE, 'B lands on "Choose a game." too', 240)
        wait_log(b, 'group: dialog "', 'B: a notice says why the session ended', 60, notes + 1)
        note = [l for l in b.log().splitlines() if 'group: dialog "' in l][-1:]
        check('the notice: the host left, one OK button', bool(note) and 'The host left' in note[0] and note[0].endswith('[OK]'),
              str(note))
        b.stub.until_frames(40)
        b.capture('05-host-left')
        b.stub.press(KEY_A, 2, 30)       # OK

        # ---- 6. a match in progress elsewhere: B joins it late ----
        pc = out / 'pc'
        pc.mkdir()
        (pc / 'group.ini').write_text('\n'.join([
            'role=host', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000', 'name=Pico', 'test_players=1',
            'test_games=1', f'test_frames={90 * 60}', 'test_linger_ms=60000', '']) + '\n')
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        wait_for(lambda: 'group-test: match block ready' in (out / 'pc.log').read_text(errors='replace'),
                 'the PC console starts a match against a CPU', 90)
        b.stub.until_frames(180)         # a few scans, so the game is in B's list
        b.touch(160, 80)
        wait_scene(b, 8, 'B joins the running game: the lobby character select', 120)
        wait_log(b, 'group: watching a match in progress', 'B: the Match in progress page', 60)
        b.stub.until_frames(90)
        b.capture('06-match-in-progress')
        mark = len(b.log())
        for keys in (KEY_A, CPAD_UP, KEY_A, KEY_START):
            b.stub.press(keys, 6 if keys == CPAD_UP else 2, 10)
        after = b.log()[mark:]
        check('B\'s character select sends nothing while it watches',
              'GROUPCSS send door' not in after and 'GROUPCSS Start pressed' not in after)
        left = b.log().count('group: left the lobby')
        b.stub.press(KEY_B, 2, 4)
        b.stub.press(KEY_B, 45, 4)
        wait_log(b, 'group: left the lobby', 'B leaves with Back', 60, left + 1)

        # ---- 7. the soft reset on "Choose a game." ----
        on = a.log().count('group: HOME and sleep on')
        a.stub.press(KEY_L | KEY_R | KEY_START, 4, 30)
        wait_for(lambda: a.scene() == 1 and a.stub.menu()[0] == 0, 'A: the soft reset lands on the main menu', 120)
        wait_log(a, 'group: HOME and sleep on', 'A: the soft reset closes the session (HOME and sleep on again)', 30, on + 1)
        for c in (a, b):
            check(f'{c.name} did not crash', c.proc.poll() is None and 'PANIC' not in c.log())
    finally:
        if pc_proc is not None and pc_proc.poll() is None:
            pc_proc.kill()
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
