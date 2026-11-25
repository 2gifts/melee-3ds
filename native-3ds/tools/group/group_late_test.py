"""Joining a group while its match is running, in Azahar: the emulated 3DS is the late joiner.

A PC console (tools/group/src/group_client.c) hosts and starts a long match against a CPU once the emulated 3DS is on
the Host / Join page. The emulated 3DS joins that game from the list while the match runs, and
must: land on the lobby's character select, show the bottom-screen "Match in progress" page (screenshots), not take
part in the match, keep its CSS read-only (A, B, Start and the stick send nothing) with no READY although the host's
door and its CPU have fighters, see the lobby come back when the match ends (the host's door mirrored, READY back), and
leave with Back. Zelda and Sheik: the host plays Zelda and transforms halfway (its match status says Sheik from then on,
test_zelda=1): the Match in progress page's badge for the host shows Zelda's stock icon, then Sheik's within a few
seconds (mp_group_badge_drawn: the fighter each badge last drew, + 1).

    docker/emu-test.sh run tools/group/group_late_test.py [--match-seconds 60]
Results: $MELEE_TEST_OUT/group-late/ (PNGs, game.log, the PC host's log)."""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'slippi'))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from css_probe import Css  # noqa: E402
from group_menu_test import to_main_menu, CPAD_RIGHT, CPAD_UP, KEY_A, KEY_B, KEY_START, MENU_VS, PORT, Stub, save_bottom, save_top  # noqa: E402

KEY_UP = 0x40   # the D-pad up bit of the 3DS keys (KEY_DUP)

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44100


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--match-seconds', type=int, default=180, help='length of the host\'s match (it runs in real time)')
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-late'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group-late', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text(f'menu=1\nudp=1\nport={LINK_PORT}\ndelay=2\ntimeout_ms=15000\n')
    (sd / 'no-scripted-input').write_text('')   # no smoke-build scripted presses
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / 'pc'
    pc.mkdir()
    (pc / 'group.ini').write_text('\n'.join([
        'role=host', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000', 'test_players=1', 'test_games=1',
        f'test_frames={args.match_seconds * 60}', 'test_linger_ms=150000', 'test_zelda=1', '']) + '\n')
    stub = Stub(emu.elf_symbols(args.elf))
    css = Css(stub)
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    t0 = time.monotonic()
    results = []
    pc_proc = None

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def pc_text():
        p = out / 'pc.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def wait_text(source, text, what, timeout=120):
        end = time.monotonic() + timeout
        while time.monotonic() < end and proc.poll() is None:
            if text in source():
                check(what, True)
                return True
            time.sleep(1)
        check(what, False, 'not seen: ' + text)
        return False

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        stub.until_frames(20)

    def capture(name):
        stub.poke('mp_test_capture', 1)
        for _ in range(120):
            time.sleep(0.5)
            if int.from_bytes(stub.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(sd / 'engine-top.bgr', out / f'{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', out / f'{name}-bottom.png')
        print('captured', name, flush=True)

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        check('reach the main menu', to_main_menu(stub))
        s = stub.goto_vs_menu()
        check('reached the VS menu', s['scene'] == 1 and stub.menu()[0] == MENU_VS, str(stub.menu()))
        for _ in range(8):               # the cursor on Melee (the top row)
            if stub.menu()[1] == 0:
                break
            stub.press(KEY_UP)
        check('Melee is hovered on the VS menu', stub.menu() == (MENU_VS, 0), str(stub.menu()))
        stub.press(KEY_A)                # Melee: the Solo/Group page
        wait_text(log_text, 'group: the Solo/Group page is up', 'the Solo/Group page opens', 60)
        stub.choose()                  # GROUP
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the Host / Join page', 60)
        # Now the PC host opens its lobby and starts a match against a CPU at once.
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        if not wait_text(pc_text, 'group-test: match block ready', 'the PC host starts its match', 60):
            raise SystemExit(1)
        stub.until_frames(120)           # a few scans, so the game is in the list
        capture('01-games-list')
        touch(160, 80)                   # the first game in the list (both layouts)
        s = None
        end = time.monotonic() + 120
        while time.monotonic() < end:
            s = stub.state()
            if s['scene'] == 8:
                break
            time.sleep(1)
        check('joined the running game: the late joiner is on the lobby character select', s is not None and s['scene'] == 8 and s['mode'] == 8, str(s))
        wait_text(log_text, 'group: watching a match in progress', 'the Match in progress page is up', 60)
        stub.until_frames(90)
        capture('02-match-in-progress')
        stub.until_frames(240)
        capture('03-match-in-progress-later')
        check('this console takes no part in the running match', 'group: match block' not in log_text())
        # the late joiner's own name never shows on the overview (it is not in the match); a CPU's row says
        # CPU and the host's row its name
        import re as _re
        me = int((_re.findall(r'group: in the lobby on port (\d)', log_text()) or ['0'])[-1]) - 1
        mine = (_re.findall(r'group: name from \w+ "([^"]*)"', log_text()) or [''])[-1]
        shown = [stub.peek('mp_group_watch_name', 128)[32 * i:32 * i + 32].split(b'\0')[0].decode('utf-8', 'replace') for i in range(4)]
        check('the overview never shows the late joiner\'s own name', me >= 1 and mine and mine not in shown,
              f'port {me + 1}, name {mine!r}, rows {shown}')
        check('the CPU on the late joiner\'s own port says CPU; the host\'s row has its name',
              shown[me] == 'CPU' and shown[0] not in ('', 'CPU'), str(shown))

        def badge(port):
            return int.from_bytes(stub.peek('mp_group_badge_drawn', 8)[2 * port:2 * port + 2], 'little') - 1
        zelda = badge(0)
        check('the host\'s badge on the Match in progress page is Zelda (18) before she transforms',
              zelda == 18 or 'Zelda becomes Sheik' in pc_text(), f'badge fighter {zelda}')
        # Read-only while watching: a door click, a puck grab, Start: nothing reaches the lobby; READY never shows.
        mark = len(log_text())
        ready_seen = 0
        for keys in (KEY_A, CPAD_UP, KEY_A, CPAD_RIGHT, KEY_A, KEY_B, KEY_START, CPAD_UP | CPAD_RIGHT, KEY_A):
            stub.press(keys, 6 if keys & (CPAD_UP | CPAD_RIGHT) else 2, 10)
            ready_seen += css.ready() != 0
        start = stub.frames()
        while stub.frames() - start < 60:
            ready_seen += css.ready() != 0
        after = log_text()[mark:]
        check('the watching CSS sends no door edit and no Start', 'GROUPCSS send door' not in after and 'GROUPCSS Start pressed' not in after,
              '; '.join(l for l in after.splitlines() if 'GROUPCSS' in l)[:300])
        check('no READY on the watching CSS (the host and its CPU have fighters)', ready_seen == 0, f'READY seen {ready_seen} times')
        check('still watching after those presses', 'group: no match in progress here any more' not in log_text()
              and 'group: left the lobby' not in log_text())
        wait_text(pc_text, 'Zelda becomes Sheik', 'the host\'s Zelda transforms halfway', args.match_seconds + 60)
        end = time.monotonic() + 20
        while time.monotonic() < end and badge(0) != 19:
            time.sleep(0.25)
        check('the badge follows: Sheik (19) within a few seconds of the transform', badge(0) == 19, f'badge fighter {badge(0)}')
        capture('03b-match-in-progress-sheik')
        wait_text(log_text, 'group: no match in progress here any more', 'the page goes when the match ends',
                  args.match_seconds + 60)
        wait_text(log_text, 'GROUPCSS apply door 0', 'the lobby is back: the host\'s door is mirrored', 60)
        stub.until_frames(60)
        capture('04-lobby-after-the-match')
        ready_seen = 0
        start = stub.frames()
        while stub.frames() - start < 60:
            ready_seen += css.ready() != 0
        check('READY shows once the match is over (the host and its CPU are in)', ready_seen > 0)
        # Back: B released once, then held (vanilla wants 30 frames).
        stub.press(KEY_B, 2, 4)
        stub.press(KEY_B, 45, 4)
        wait_text(log_text, 'group: left the lobby', 'Back leaves the lobby', 60)
    finally:
        if pc_proc is not None and pc_proc.poll() is None:
            pc_proc.kill()
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
