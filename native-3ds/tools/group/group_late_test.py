"""Joining a group while its match is running, in Azahar: the emulated 3DS is the late joiner.

A PC console (tools/group/src/group_client.c) hosts and starts a long match against a CPU once the emulated 3DS is on
the Host / Join page. The emulated 3DS joins that game from the list while the match runs, and
must: land on the lobby's character select, show the bottom-screen "Match in progress" page (screenshots), not take
part in the match, see the lobby come back when the match ends (the host's door mirrored), and leave with Back.

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
from group_menu_test import KEY_A, KEY_B, MENU_VS, PORT, Stub, save_bottom, save_top  # noqa: E402

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
        f'test_frames={args.match_seconds * 60}', 'test_linger_ms=150000', '']) + '\n')
    stub = Stub(emu.elf_symbols(args.elf))
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
        s = stub.goto_vs_menu()
        check('reached the VS menu', s['scene'] == 1 and stub.menu()[0] == MENU_VS, str(stub.menu()))
        for _ in range(8):               # the cursor on Melee (the top row)
            if stub.menu()[1] == 0:
                break
            stub.press(KEY_UP)
        check('Melee is hovered on the VS menu', stub.menu() == (MENU_VS, 0), str(stub.menu()))
        stub.press(KEY_A)                # Melee: the Solo/Group page
        wait_text(log_text, 'group: the Solo/Group page is up', 'the Solo/Group page opens', 60)
        touch(238, 104)                  # GROUP
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the Host / Join page', 60)
        # Now the PC host opens its lobby and starts a match against a CPU at once.
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        if not wait_text(pc_text, 'group-test: match block ready', 'the PC host starts its match', 60):
            raise SystemExit(1)
        stub.until_frames(120)           # a few scans, so the game is in the list
        capture('01-games-list')
        touch(160, 108)                  # the first game in the list
        wait_text(log_text, 'lw: joined the lobby', 'joined the running game\'s lobby', 60)
        s = None
        end = time.monotonic() + 120
        while time.monotonic() < end:
            s = stub.state()
            if s['scene'] == 8:
                break
            time.sleep(1)
        check('the late joiner is on the lobby character select', s is not None and s['scene'] == 8 and s['mode'] == 8, str(s))
        wait_text(log_text, 'group: watching a match in progress', 'the Match in progress page is up', 60)
        stub.until_frames(90)
        capture('02-match-in-progress')
        stub.until_frames(240)
        capture('03-match-in-progress-later')
        check('this console takes no part in the running match', 'group: match block' not in log_text())
        wait_text(log_text, 'group: no match in progress here any more', 'the page goes when the match ends',
                  args.match_seconds + 60)
        wait_text(log_text, 'GROUPCSS apply door 0', 'the lobby is back: the host\'s door is mirrored', 60)
        stub.until_frames(60)
        capture('04-lobby-after-the-match')
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
