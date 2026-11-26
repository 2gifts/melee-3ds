"""The engine never waits for the wireless link (Phase 9): with group.ini test_link_delay_ms, the link thread sleeps that
long inside every open, join, close and scan (as slow hardware would), and the emulated console must not notice.

One emulated console and one PC host (tools/group/src/group_client.c) over the loopback link, both with the delay.
The console:
  1. Group: the list page opens at once while the link opens; browses through a few slowed scans; Back.
  2. Group, then Back before the link is up (during the open); then Group again: the list works and scans.
  3. Joins the PC host's game (a slowed join), then leaves the lobby (a slowed close).
  4. Hosts (a slowed open as host), then leaves its own lobby.
Checked for each step:
  - no long engine frame (over 50 ms, "Long engine frame") without SD reads: a frame that read the SD card is a scene
    loading, which vanilla Melee does too ("Choose a game." is the Tournament setup scene);
  - the engine keeps running: engine frames advance during each slowed call;
  - the delays really happened ("test delay of N ms in the link thread (open/join/close/scan)");
  - Back from the list: HOME and sleep come back on only after the link has closed ("group: HOME and sleep on" after
    "group: session closed"). Leaving a lobby lands on "Choose a game." with the list's link opened again: they stay
    off throughout, and Host from the list (a close, then an open as host) never turns them on for a moment.

    docker/emu-test.sh run tools/group/group_link_test.py [--delay MS]
Results: $MELEE_TEST_OUT/group-link/ (game.log, the PC host's log, PNGs)."""
import argparse
import os
import re
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
from emu_group import to_main_menu, KEY_A, KEY_B, MENU_VS, PORT, SCENE_CHOOSE, Stub, save_bottom, save_top  # noqa: E402

KEY_UP = 0x40
TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44210
LONG = re.compile(r'Long engine frame (\d+): ([\d.]+) ms; SD reads \+(\d+)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--delay', type=int, default=500, help='test_link_delay_ms (default 500)')
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-link'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group-link', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text(f'menu=1\nudp=1\nport={LINK_PORT}\ndelay=2\ntimeout_ms=15000\n'
                                             f'name=Slowpoke\ntest_link_delay_ms={args.delay}\n')
    (sd / 'no-scripted-input').write_text('')
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / 'pc'
    pc.mkdir()
    (pc / 'group.ini').write_text('\n'.join(['role=host', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000',
                                             'name=PCHost', 'test_control=1', f'test_link_delay_ms={args.delay}', '']))
    (pc / 'control.txt').write_text('')
    stub = Stub(emu.elf_symbols(args.elf))
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    t0 = time.monotonic()
    results = []
    pc_proc = None

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, '' if ok else detail, flush=True)
        return ok

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def pc_text():
        p = out / 'pc.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def wait_text(source, text, what, timeout=120, count=1, fatal=False):
        end = time.monotonic() + timeout
        while time.monotonic() < end and proc.poll() is None:
            if source().count(text) >= count:
                return check(what, True)
            time.sleep(0.3)
        check(what, False, f'not seen ({count}x): {text}')
        if fatal:
            raise SystemExit(1)
        return False

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        stub.until_frames(2)

    def capture(name):
        stub.poke('mp_test_capture', 1)
        for _ in range(120):
            time.sleep(0.5)
            if int.from_bytes(stub.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(sd / 'engine-top.bgr', out / f'{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', out / f'{name}-bottom.png')
        stub.until_frames(5)                       # a capture makes a long frame of its own: past it before measuring

    def stalls(first, last):
        """Long engine frames in [first, last] that read nothing from the SD card (not a scene loading)."""
        return [(int(m.group(1)), float(m.group(2))) for m in LONG.finditer(log_text())
                if first <= int(m.group(1)) <= last and int(m.group(3)) == 0]

    def window(what, first):
        """The engine kept running through a slowed call: no stall since `first`, and frames still advance."""
        stub.until_frames(30)
        last = stub.frames()
        s = stalls(first, last)
        check(f'{what}: no engine stall ({last - first} frames watched)', not s, str(s[:8]))

    def delays(kind):
        return log_text().count(f'test delay of {args.delay} ms in the link thread ({kind})')

    def to_solo_group_page():
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] not in (1, SCENE_CHOOSE):
            time.sleep(0.5)
        if stub.state()['scene'] == SCENE_CHOOSE:
            stub.press(KEY_B, 2, 30)               # "Choose a game." -> the Solo / Group submenu
            end = time.monotonic() + 60
            while time.monotonic() < end and stub.state()['scene'] != 1:
                time.sleep(0.3)
        if stub.menu()[0] != 11:
            stub.goto_vs_menu()
            for _ in range(8):
                if stub.menu()[1] == 0:
                    break
                stub.press(KEY_UP)
            n = log_text().count('group: the Solo/Group page is up')
            stub.press(KEY_A)
            wait_text(log_text, 'group: the Solo/Group page is up', 'the Solo/Group page opens', 60, n + 1)

    def group():
        n = log_text().count('group: Group, looking for games')
        first = stub.frames()
        stub.choose()                              # GROUP
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the list page', 60, n + 1, fatal=True)
        return first

    def back_from_list():
        n = log_text().count('group: Back from the Group page')
        first = stub.frames()
        stub.press(KEY_B, 2, 4)
        wait_text(log_text, 'group: Back from the Group page', 'Back from the list', 60, n + 1)
        return first

    def closed_then_on(what, closes, ons):
        """Back from the list: the link closed (a slowed close), then HOME and sleep came back on, in that order."""
        wait_text(log_text, 'group: session closed', f'{what}: the link closes (on the link thread)', 60, closes)
        wait_text(log_text, 'group: HOME and sleep on', f'{what}: HOME and sleep on again', 30, ons + 1)
        text = log_text()
        last_close = text.rfind('group: session closed')
        last_on = text.rfind('group: HOME and sleep on')
        check(f'{what}: HOME and sleep come back on only after the close', last_on > last_close >= 0,
              f'close at {last_close}, on at {last_on}')

    def closed_then_list(what, closes, opens, ons):
        """Leaving a lobby: the link closes, and "Choose a game." opens the list's link again; HOME and sleep stay off
        throughout (the console is never left with wireless half shut, nor on for a moment)."""
        wait_text(log_text, 'group: session closed', f'{what}: the link closes (on the link thread)', 60, closes)
        wait_text(log_text, 'group: session open', f'{what}: "Choose a game." opens the list\'s link again', 60, opens + 1)
        check(f'{what}: HOME and sleep stay off from the lobby to the list',
              log_text().count('group: HOME and sleep on') == ons, f"{log_text().count('group: HOME and sleep on') - ons} on")

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        check('reach the main menu', to_main_menu(stub))
        to_solo_group_page()

        # 1. Group (a slowed open), browse through slowed scans, Back (a slowed close)
        opens = delays('open')
        first = group()
        wait_text(log_text, 'group: session open', '1: the link opens on the link thread', 60, 1)
        check('1: the open was slowed by the test delay', delays('open') > opens)
        window('1: Group (the list opens while the link comes up)', first)
        scans = delays('scan')
        stub.until_frames(240)
        check('1: slowed scans ran while browsing', delays('scan') > scans)
        window('1: browsing', first)
        capture('1-list')
        closes = log_text().count('group: session closed')
        ons = log_text().count('group: HOME and sleep on')
        first = back_from_list()
        closed_then_on('1: Back', closes + 1, ons)
        window('1: Back (the link closes on the link thread)', first)

        # 2. Group, Back during the open, Group again
        to_solo_group_page()
        closes = log_text().count('group: session closed')
        first = group()
        back_from_list()                           # within the delay: the open is still running
        stub.until_frames(int(args.delay / 16) + 60)
        window('2: Back during the open', first)
        wait_text(log_text, 'group: session closed', '2: the link opened meanwhile is closed again', 60, closes + 1)
        to_solo_group_page()
        opened = log_text().count('group: session open')
        first = group()
        wait_text(log_text, 'group: session open', '2: Group again opens the link', 60, opened + 1)
        stub.until_frames(120)
        window('2: Group again', first)

        # 3. Join the PC host's game (a slowed join), leave the lobby (a slowed close)
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        wait_text(pc_text, 'group-test: in the lobby on port', '3: the PC host opens its lobby', 60, fatal=True)
        wait_text(log_text, 'group: 1 game listed', '3: the PC host\'s game is listed', 60)
        joins = delays('join')
        first = stub.frames()
        touch(160, 80)                             # the first game in the list
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] != 8:
            time.sleep(0.3)
        check('3: joined: on the lobby character select', stub.state()['scene'] == 8, str(stub.state()))
        check('3: the join was slowed by the test delay', delays('join') > joins)
        window('3: Join', first)
        stub.until_frames(60)
        capture('3-lobby')
        closes = log_text().count('group: session closed')
        opens = log_text().count('group: session open')
        ons = log_text().count('group: HOME and sleep on')
        lefts = log_text().count('group: left the lobby')
        first = stub.frames()
        stub.poke('mp_test_keys', KEY_B)           # Back held leaves the lobby
        end = time.monotonic() + 60
        while time.monotonic() < end and log_text().count('group: left the lobby') <= lefts:
            time.sleep(0.1)
        stub.poke('mp_test_keys', 0)
        check('3: Back held leaves the lobby', log_text().count('group: left the lobby') > lefts)
        closed_then_list('3: leaving the lobby', closes + 1, opens, ons)
        window('3: leaving the lobby', first)

        # 4. Host (a slowed open as host), leave its lobby
        to_solo_group_page()
        group()
        wait_text(log_text, 'group: session open', '4: the list\'s link opens', 60, log_text().count('group: session open'))
        stub.until_frames(30)
        hosting = log_text().count('group: hosting a game')
        ons_before_host = log_text().count('group: HOME and sleep on')
        first = stub.frames()
        touch(118, 50)                             # HOST
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] != 8:
            time.sleep(0.3)
        check('4: hosting: on the lobby character select', stub.state()['scene'] == 8, str(stub.state()))
        check('4: the host\'s open ran on the link thread', log_text().count('group: hosting a game') > hosting)
        window('4: Host', first)
        check('4: Host from the list never turned HOME and sleep on for a moment (a close, then an open as host)',
              log_text().count('group: HOME and sleep on') == ons_before_host)
        capture('4-hosting')
        closes = log_text().count('group: session closed')
        opens = log_text().count('group: session open')
        ons = log_text().count('group: HOME and sleep on')
        lefts = log_text().count('group: left the lobby')
        first = stub.frames()
        stub.poke('mp_test_keys', KEY_B)
        end = time.monotonic() + 60
        while time.monotonic() < end and log_text().count('group: left the lobby') <= lefts:
            time.sleep(0.1)
        stub.poke('mp_test_keys', 0)
        check('4: Back held leaves the host\'s own lobby', log_text().count('group: left the lobby') > lefts)
        closed_then_list('4: leaving the lobby', closes + 1, opens, ons)
        window('4: leaving the lobby', first)
        check('the game did not crash', proc.poll() is None and 'PANIC' not in log_text())
    finally:
        emu.stop(proc)
        if pc_proc is not None:
            pc_proc.kill()
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
