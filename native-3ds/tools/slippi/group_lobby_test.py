"""The networked group lobby in Azahar the real Melee character select as the lobby of two consoles.

The emulated 3DS hosts (Melee -> Group -> Host, as group_menu_test.py); a PC console (tools/slippi/src, the selftest) joins
over the loopback link, fills its own door with a fighter and waits for the match. Then: the host's CSS must show the
PC's door (a mirrored portrait, the PC's name and signal under it), the host picks a fighter, presses Start, the stage
select appears, a stage is picked, and the match starts on both consoles. Screenshots of every step.

    docker/emu-test.sh run tools/slippi/group_lobby_test.py [--seconds 1500] [--top-menu]
Results: $MELEE_TEST_OUT/group-lobby/ (PNGs, game.log, the PC console's log)."""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
KEY_L, KEY_R = 1 << 9, 1 << 8
from group_menu_test import CPAD_RIGHT, CPAD_UP, KEY_A, KEY_B, KEY_DOWN, KEY_START, PORT, Stub, save_bottom, save_top  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_TOOLS_DIR') or emu.ROOT / 'build/slippi-tools')
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
    (sd / 'slippi' / 'config.ini').write_text(f'local_group=1\nlocal_udp=1\nlocal_port={LINK_PORT}\nprefetch=0\ndelay=2\nlocal_timeout_ms=15000\n')
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / 'pc'
    pc.mkdir()
    (pc / 'config.ini').write_text('\n'.join([
        'opponent=@join', 'local_group=1', 'local_lobby=1', 'local_udp=1', f'local_port={LINK_PORT}', 'delay=2',
        'local_timeout_ms=15000', 'selftest=1', 'selftest_frames=900', 'selftest_games=1', f'selftest_character={args.character - 1}',
        '']) + '\n')
    stub = Stub(emu.elf_symbols(args.elf))
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    t0 = time.monotonic()
    results = []
    pc_proc = None

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)

    def pulse(keys, hold=1.0):
        stub.poke('mp_test_keys', keys)
        time.sleep(hold)
        stub.poke('mp_test_keys', 0)
        time.sleep(0.8)

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        time.sleep(1.5)

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
                pulse(press, hold)
            else:
                time.sleep(1)
        check(what, False, f'last state {s}')
        raise SystemExit(1)

    def wait_log(text, what, timeout=120):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in log_text():
                check(what, True)
                return True
            time.sleep(2)
        check(what, False, 'not in game.log: ' + text)
        return False

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        s = wait(lambda s: s['scene'] == 1, 'reach the main menu', 360, KEY_START | KEY_A)
        time.sleep(3)
        for _ in range(10):
            time.sleep(5)
            s = stub.state()
            if s['scene'] == 1 and s['menu'] == 2:
                break
            if s['scene'] == 1 and s['menu'] == 0:
                pulse(KEY_DOWN, 0.5)
                time.sleep(2)
                pulse(KEY_A, 0.5)
            elif s['scene'] == 1:
                pulse(KEY_B, 0.5)
            time.sleep(3)
        check('reached the VS menu', stub.state()['menu'] == 2)
        pulse(KEY_A, 0.3)                # Melee: the Solo/Group page
        time.sleep(4)
        touch(238, 104)                  # GROUP
        time.sleep(3)
        touch(118, 50)                   # HOST
        s = wait(lambda s: s['scene'] == 8, 'host enters the lobby CSS', 120)
        check('the lobby CSS is the group scene (mode 8)', s['mode'] == 8, str(s))
        time.sleep(8)
        capture('01-lobby-alone')

        # The PC console joins and fills its door.
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'slippi_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        wait_log('GROUPCSS apply door 1', 'the PC console\'s door is mirrored on the host CSS', 180)
        time.sleep(4)
        capture('02-pc-door')

        # The host takes a fighter: A picks the token up / drops it on the fighter under the hand.
        # The hand starts bottom left, holding the token: move it up until it hovers over a fighter (the lobby then
        # gets a hover), then A drops it there.
        for attempt in range(10):
            pulse(CPAD_UP, 2.0)
            if 'GROUPCSS send door 0' in log_text():
                for _ in range(4):          # A drops the token on the fighter under the hand
                    pulse(KEY_A, 0.3)
                    time.sleep(2.5)
                    if any('send door 0' in l and 'chosen 1' in l for l in log_text().splitlines()):
                        break
                break
        capture('03-picked')
        check('the host\'s own pick reaches the lobby', any('send door 0' in l and 'chosen 1' in l for l in log_text().splitlines()))
        time.sleep(3)
        capture('03-both-chosen')

        # Start (the READY overlay), then the stage select.
        pulse(KEY_START, 0.3)
        wait_log('GROUPCSS Start pressed', 'Start asks the lobby', 60)
        s = wait(lambda s: s['scene'] != 8, 'the stage select (or the match) replaces the CSS', 180)
        print('after start:', s, flush=True)
        time.sleep(6)
        capture('04-stage-select')
        pulse(CPAD_UP, 2.0)              # the stage cursor starts below the grid
        pulse(KEY_A, 0.4)                # pick the stage under it
        wait_log('local: starting a match', 'the host starts the match from the picked stage', 120)
        wait_log('Slippi online: match starts', 'the match loads and starts on the emulated console', 240)
        time.sleep(20)
        capture('05-match')
        # Quit the match with L+R+A+Start: the lobby comes back, and this console's door must keep its fighter.
        mark = len(log_text())
        pulse(KEY_L | KEY_R | KEY_A | KEY_START, 40.0)   # the PC peer is slow, so few frames pass
        s = wait(lambda s: s['scene'] == 8, 'back on the character select after the match', 300)
        time.sleep(15)
        capture('06-back-on-css')
        after = log_text()[mark:]
        for line in after.splitlines():
            if 'GROUPCSS' in line or 'lobby' in line.lower():
                print('  after match:', line, flush=True)
        check('this console\'s door keeps its fighter after the match', 'GROUPCSS send door 0' not in after or 'fighter 14' in after)
        final = (out / 'pc.log').read_text(errors='replace') if (out / 'pc.log').exists() else ''
        check('the PC console received the host\'s match block', 'selftest: match block ready' in final, final[-300:])
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
