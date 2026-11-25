"""The networked group lobby in Azahar the real Melee character select as the lobby of two consoles.

The emulated 3DS hosts (Melee -> Group -> Host, as group_menu_test.py); a PC console (tools/group/src/group_client.c) joins
over the loopback link, fills its own door with a fighter and waits for the match. Then: the host's CSS must show the
PC's door (a mirrored portrait, the PC's name and signal under it), the host picks a fighter, opens its Rules menu (the
host is busy on the PC console until it is back), presses Start, the stage select appears, a stage is picked, and the
match starts on both consoles; after it, the lobby comes back with the doors as they were. Screenshots of every step.
The PC console only simulates a game, so its checksums never match the real game's: the match ends on a desync after
a few seconds, which is enough here (matches between emulated consoles: group_quad_test.py).

    docker/emu-test.sh run tools/group/group_lobby_test.py [--seconds 1500]
Results: $MELEE_TEST_OUT/group-lobby/ (PNGs, game.log, the PC console's log)."""
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
KEY_L, KEY_R = 1 << 9, 1 << 8
CPAD_LEFT, CPAD_DOWN = 1 << 29, 1 << 31
from css_probe import Css  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402
from group_menu_test import CPAD_RIGHT, CPAD_UP, KEY_A, KEY_B, KEY_DOWN, KEY_START, MENU_VS, PORT, Stub, save_bottom, save_top  # noqa: E402

FOX_TILE = (-24.4, -17.4, 6.0, 13.0)   # left, right, bottom, top in CSS units (mncharsel.c icons[], a starter: never locked)

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44100


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--seconds', type=float, default=1500)
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--character', type=int, default=8, help='the PC console\'s fighter (Mario = 8)')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-lobby'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group-lobby', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text(f'menu=1\nudp=1\nport={LINK_PORT}\ndelay=2\ntimeout_ms=15000\n')
    (sd / 'no-scripted-input').write_text('')   # no smoke-build scripted presses
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / 'pc'
    pc.mkdir()
    (pc / 'group.ini').write_text('\n'.join([
        'role=join', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000', 'test_frames=900', 'test_games=1',
        f'test_character={args.character - 1}', '']) + '\n')
    stub = Stub(emu.elf_symbols(args.elf))
    css = Css(stub)
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    t0 = time.monotonic()
    results = []
    pc_proc = None

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        stub.until_frames(10)

    def token(door=0):
        """Door's token (CSSCharModel, big-endian engine data): (state x5, x, y)."""
        ptr = struct.unpack('>I', stub.peek('mnCharSel_804A0BD0', 4 * 4)[4 * door:4 * door + 4])[0]
        b = stub._session(lambda sock: (packet(sock, f'm{ptr:x},10'), bytes.fromhex(receive(sock)))[1])
        x, y = struct.unpack('>ff', b[8:16])
        return b[5], x, y

    def steer_token(tile, steps=300):
        """Move this console's held token onto the tile with the circle pad, reading where it is between nudges."""
        left, right, bottom, top = tile
        cx, cy = (left + right) / 2, (bottom + top) / 2
        for _ in range(steps):
            _, x, y = token()
            if left + 0.8 < x < right - 0.8 and bottom + 0.8 < y < top - 0.8:
                return True
            keys = 0
            if abs(x - cx) > 0.6:
                keys |= CPAD_RIGHT if x < cx else CPAD_LEFT
            if abs(y - cy) > 0.6:
                keys |= CPAD_UP if y < cy else CPAD_DOWN
            stub.press(keys, 1, 1)
        return False

    def steer_cursor(inside, target, steps=300):
        """Move this console's cursor (empty hand) until inside(x, y), heading for target with the circle pad."""
        for _ in range(steps):
            _, x, y = css.cursor(0)
            if inside(x, y):
                return True
            keys = 0
            if abs(x - target[0]) > 1.0:
                keys |= CPAD_RIGHT if x < target[0] else CPAD_LEFT
            if abs(y - target[1]) > 1.0:
                keys |= CPAD_UP if y < target[1] else CPAD_DOWN
            stub.press(keys, 1, 1)
        return False

    def pc_text():
        p = out / 'pc.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def wait_pc(text, what, timeout=60):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in pc_text():
                return check(what, True)
            time.sleep(0.5)
        return check(what, False, 'not in the PC console\'s log: ' + text)

    def capture(name):
        stub.poke('mp_test_capture', 1)
        for _ in range(120):
            time.sleep(0.5)
            if int.from_bytes(stub.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(sd / 'engine-top.bgr', out / f'{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', out / f'{name}-bottom.png')
        print('captured', name, flush=True)

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def wait(pred, what, timeout=240, press=None, hold=1.0):
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

    def wait_log(text, what, timeout=120, since=0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in log_text()[since:]:
                check(what, True)
                return True
            time.sleep(2)
        check(what, False, 'not in game.log: ' + text)
        return False

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        s = wait(lambda s: s['scene'] == 1, 'reach the main menu', 360, KEY_START | KEY_A)
        s = stub.goto_vs_menu()
        check('reached the VS menu', s['scene'] == 1 and stub.menu()[0] == MENU_VS, str(stub.menu()))
        stub.press(KEY_A)                # Melee: the Solo/Group page
        wait_log('group: the Solo/Group page is up', 'the Solo/Group page opens', 60)
        stub.until_frames(10)
        stub.choose()                  # GROUP
        wait_log('group: Group, looking for games', 'Group opens the Host / Join page', 60)
        touch(118, 50)                   # HOST
        s = wait(lambda s: s['scene'] == 8, 'host enters the lobby CSS', 120)
        check('the lobby CSS is the group scene (mode 8)', s['mode'] == 8, str(s))
        stub.until_frames(60)
        capture('01-lobby-alone')

        # The PC console joins and fills its door.
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        wait_log('GROUPCSS apply door 1', 'the PC console\'s door is mirrored on the host CSS', 180)
        stub.until_frames(30)
        capture('02-pc-door')

        # The host takes a fighter: its hand holds its token; steer the token onto Fox (a starter) and A drops it.
        def placed():
            """The token is down on a fighter (read from memory: game.log can lag, and A again would pick it back up)."""
            return css.cursor(0)[0] != 1 and css.doors()[0]['sel_icon'] < 0x19

        def picked():
            return any('send door 0' in l and 'chosen 1' in l for l in log_text().splitlines())
        for attempt in range(4):
            if placed():
                break
            if not steer_token(FOX_TILE):
                print('the token did not reach the Fox tile; at', token(), flush=True)
            stub.press(KEY_A, 4, 30)
        check('the host\'s token is down on a fighter', placed(), f'cursor {css.cursor(0)}, door {css.doors()[0]}')
        my_icon = css.doors()[0]['sel_icon']
        capture('03-picked')
        end = time.monotonic() + 60
        while time.monotonic() < end and not picked():
            time.sleep(1)
        check('the host\'s own pick reaches the lobby', picked())
        stub.until_frames(60)
        capture('03-both-chosen')

        # The Rules menu makes the host busy (READY and Start wait on every console) until it is back on the CSS.
        on_rules = lambda x, y: -15.0 < x < 13.0 and y > 23.0   # noqa: E731 (mncharsel.c: the Rules button's area)
        if not steer_cursor(on_rules, (0.0, 26.0)):
            print('the cursor did not reach the Rules button; at', css.cursor(0), flush=True)
        # (the busy line also goes out when only the doors this console uses change: count from here)
        busy_mark = len(log_text())
        for _ in range(4):
            stub.press(KEY_A, 2, 20)
            if stub.peek('mnCharSel_804D6CF6', 1)[0] in (3, 5) or stub.state()['scene'] != 8 or 'GROUPCSS busy 1' in log_text()[busy_mark:]:
                break
        wait_log('GROUPCSS busy 1', 'opening the Rules menu sets this console busy', 60, busy_mark)
        wait_pc('group-test: busy now 1', 'the PC console sees the host busy while its Rules menu is up')
        stub.until_frames(60)
        capture('03b-rules-menu')
        busy_mark = len(log_text())
        for _ in range(10):              # B back to the CSS
            if 'GROUPCSS busy 0' in log_text()[busy_mark:]:
                break
            stub.press(KEY_B, 2, 40)
        wait_log('GROUPCSS busy 0', 'back from the Rules menu this console is not busy', 120, busy_mark)
        wait_pc('group-test: busy now 0', 'the PC console sees the host free again')
        s = wait(lambda s: s['scene'] == 8 and stub.peek('mnCharSel_804D6CF6', 1)[0] == 0, 'back on the lobby CSS after the Rules menu', 120)
        stub.until_frames(60)

        # Start (the READY overlay), then the stage select.
        for _ in range(5):
            stub.press(KEY_START, 4, 30)
            if 'GROUPCSS Start pressed' in log_text():
                break
        wait_log('GROUPCSS Start pressed', 'Start asks the lobby', 60)
        s = wait(lambda s: s['scene'] != 8, 'the stage select (or the match) replaces the CSS', 180)
        print('after start:', s, flush=True)
        stub.until_frames(60)
        capture('04-stage-select')
        for _ in range(8):               # the stage cursor starts below the grid: up onto a stage, then A
            if 'group: starting a match' in log_text() or 'GROUPCSS stage select' in log_text():
                break
            stub.press(CPAD_UP, 6, 4)
            stub.press(KEY_A, 4, 60)
        wait_log('group: starting a match', 'the host starts the match from the picked stage', 120)
        wait_log('Slippi online: match starts', 'the match loads and starts on the emulated console', 240)
        stub.until_frames(60)
        capture('05-match')
        # The match block carries the host's rules as vanilla builds them: the host compares the two before the match.
        wait_log('the match block\'s rules checked against vanilla: 0 differences',
                 'the match block\'s rules equal vanilla\'s for the host\'s rules menu', 60)
        # Quit the match with L+R+A+Start: the lobby comes back, and this console's door must keep its fighter.
        mark = len(log_text())
        stub.poke('mp_test_keys', KEY_L | KEY_R | KEY_A | KEY_START)
        start, end = stub.frames(), time.monotonic() + 300
        while time.monotonic() < end and stub.state()['scene'] != 8 and stub.frames() - start < 1200:
            time.sleep(0.5)
        stub.poke('mp_test_keys', 0)
        # The results screen follows (the PC console presses ready by itself); this console's player presses through it:
        # Start skips a cancelled match's wait, any press its intro, Start makes this player ready.
        wait_log('Group: the results screen', 'the results screen follows the match', 120)
        end = time.monotonic() + 300
        while time.monotonic() < end and stub.state()['scene'] != 8:
            stub.press(KEY_START, 4, 40)
            stub.press(KEY_A, 4, 40)
        s = wait(lambda s: s['scene'] == 8, 'back on the character select after the results', 300)
        stub.until_frames(240)
        capture('06-back-on-css')
        after = log_text()[mark:]
        for line in after.splitlines():
            if 'GROUPCSS' in line or 'lobby' in line.lower():
                print('  after match:', line, flush=True)
        before = [l for l in log_text()[:mark].splitlines() if 'send door 0' in l and 'chosen 1' in l]
        mine = before[-1].split('fighter ')[1].split()[0] if before else '?'
        sent = [l for l in after.splitlines() if 'send door 0' in l]
        door = css.doors()[0]
        check('this console\'s door keeps its fighter after the match', door['p_kind'] == 0 and door['sel_icon'] == my_icon
              and all(f'fighter {mine} ' in l for l in sent), f'tile {my_icon} -> {door["sel_icon"]}; sent after: {sent[:3]}')
        # The PC console's door is its own: the rebuilt CSS here must never change it and send that to the lobby (with
        # --character set to a fighter this save has not unlocked, the vanilla CSS once gave that door a random fighter).
        check('the other console\'s door is not changed from here after the match', 'GROUPCSS send door 1' not in after,
              '; '.join(l for l in after.splitlines() if 'door 1' in l)[:400])
        final = (out / 'pc.log').read_text(errors='replace') if (out / 'pc.log').exists() else ''
        check('the PC console received the host\'s match block', 'group-test: match block ready' in final, final[-300:])
        # The PC console only simulates a game (it cannot reproduce the real game's checksums), so its own pass/fail
        # is not checked; what matters is that the real game ran frames on the emulated console.
        check('the match ran on the emulated console', 'Slippi online: match exit after' in log_text())
    finally:
        if pc_proc is not None and pc_proc.poll() is None:
            pc_proc.kill()
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
