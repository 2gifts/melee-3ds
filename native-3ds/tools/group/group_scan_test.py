"""The Group list on the real UDS link (udp=0) in Azahar: the scan for games must not stall the engine.

Boots the development build with group.ini menu=1 udp=0, goes main menu -> VS -> Melee -> Group, stays on the list for
--frames engine frames (scans every 1.5 s), then Back. Checks:
  - the scans ran and how long each took ("uds: scan took N ms");
  - no long engine frame (over 50 ms, "Long engine frame") while the list is up, past the first second;
  - the engine kept running at its usual rate (engine frames per second on the list, printed);
  - Back closes the link cleanly ("uds: closed"), even with a scan in progress, and the Solo/Group page is back;
  - a second Group press opens the list again and scans again.

    docker/emu-test.sh run tools/group/group_scan_test.py
Results: $MELEE_TEST_OUT/group-scan/ (game.log, PNGs)."""
import argparse
import os
import re
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'slippi'))
import emu  # noqa: E402
from group_menu_test import Stub, KEY_A, KEY_START, MENU_VS, PORT, save_top, save_bottom  # noqa: E402,F401

LONG = re.compile(r'Long engine frame (\d+): ([\d.]+) ms')
SCAN = re.compile(r'uds: scan took (\d+) ms')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--frames', type=int, default=900, help='engine frames on the Group list (default 900, 15 s)')
    ap.add_argument('--locked', action='store_true', help='group.ini test_scan_locked=1: scan with the session lock held '
                    '(the old way), to see the stall this test guards against; the long-frame checks are then expected to fail')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / ('group-scan-locked' if args.locked else 'group-scan')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text('menu=1\nudp=0\nname=Scanner\n' + ('test_scan_locked=1\n' if args.locked else ''))
    (sd / 'no-scripted-input').write_text('')
    (sd / 'game.log').unlink(missing_ok=True)
    stub = Stub(emu.elf_symbols(args.elf))
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=True)
    t0 = time.monotonic()
    results = []

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)

    def wait_log(text, count=1, timeout=120):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if log_text().count(text) >= count:
                return True
            time.sleep(0.5)
        return False

    def touch(x, y):
        stub.poke('mp_test_bottom_touch', x | (y << 16))
        stub.until_frames(10)

    def capture(name):
        stub.poke('mp_test_capture', 1)
        for _ in range(120):
            time.sleep(0.5)
            if int.from_bytes(stub.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(sd / 'engine-top.bgr', out / f'{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', out / f'{name}-bottom.png')

    def long_frames(text, first, last):
        return [(int(m.group(1)), float(m.group(2))) for m in LONG.finditer(text) if first <= int(m.group(1)) <= last]

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        end = time.monotonic() + 360
        while time.monotonic() < end and stub.state()['scene'] != 1:
            stub.press(KEY_START | KEY_A, 2, 30)
        check('main menu', stub.state()['scene'] == 1)
        stub.goto_vs_menu()
        check('VS menu', stub.menu()[0] == MENU_VS)
        stub.press(KEY_A)
        wait_log('group: the Solo/Group page is up')
        stub.until_frames(10)
        for round_ in (1, 2):
            stub.choose()   # GROUP
            ok = wait_log('group: Group, looking for games', round_, 60)
            check(f'round {round_}: Group opens the list on UDS', ok and 'uds: init ok' in log_text())
            start = stub.frames()
            wall = time.monotonic()
            stub.until_frames(args.frames, limit=args.frames / 10 + 60)
            stop = stub.frames()
            rate = (stop - start) / max(0.001, time.monotonic() - wall)
            capture(f'list-{round_}')
            text = log_text()
            scans = [int(x) for x in SCAN.findall(text)]
            print(f'  round {round_}: {stop - start} engine frames in {time.monotonic() - wall:.1f} s ({rate:.1f}/s); '
                  f'scans timed so far: {scans}', flush=True)
            check(f'round {round_}: the scans ran (timed in the log)', len(scans) >= round_, str(scans))
            longs = long_frames(text, start + 60, stop)
            check(f'round {round_}: no long engine frame on the list', not longs, str(longs[:10]))
            touch(274, 48)    # BACK on the Group list, quite likely during a scan
            ok = wait_log('uds: closed', round_, 60) and wait_log('group: Back from the Group page', round_, 30)
            check(f'round {round_}: Back closes the link', ok,
                  str([l for l in log_text().splitlines() if 'uds: closed' in l][-1:]))
            stub.until_frames(30)
    finally:
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        if emu.log_path(home).exists():
            shutil.copy2(emu.log_path(home), out / 'azahar_log.txt')
        final = (out / 'game.log').read_text(errors='replace') if (out / 'game.log').exists() else ''
        print('\n'.join(l for l in final.splitlines() if l.startswith('uds:') or 'uds: ' in l), flush=True)
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
