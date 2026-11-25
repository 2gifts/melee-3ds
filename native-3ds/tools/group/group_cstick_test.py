"""The menu camera's C-stick in a group, in Azahar: the character select and stage select cameras turn with every
console's C-stick, as on one GameCube with four controllers.

A PC console (tools/group/src/group_client.c, test_control) hosts and holds its C-stick tilted; the emulated 3DS joins
(Melee -> Group -> the first game): its character select camera is turned as it arrives. The PC's stick back to
neutral puts the camera at rest; another tilt turns it again, and back. The emulated console's own C-stick turns its
camera too, and the PC console reads that stick. Then the PC presses Start: on the stage select, the same. The camera's
eye position is read from memory (mp_test_camera_eye, set by the group camera each frame).
Also the door plates: under each door with a console on its port, a plate with the console's name, centred (no signal
bars since 2026-11-25); none under the CPU's door or the empty one (read from memory: group_css.py's mp_css_plate /
mp_css_dtext texts and their hidden flags; mp_css_bar must be gone).

    docker/emu-test.sh run tools/group/group_cstick_test.py [--show]
Results: $MELEE_TEST_OUT/group-cstick/ (PNGs, game.log, the PC console's log)."""
import argparse
import math
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
from group_menu_test import KEY_A, KEY_UP, MENU_VS, PORT, Stub, save_bottom, save_top, to_main_menu  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44100
CSTICK_RIGHT, CSTICK_UP = 1 << 24, 1 << 26   # libctru KEY_CSTICK_*: the dev build's test keys tilt the C-stick fully


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-cstick'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group-cstick', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text(f'menu=1\nudp=1\nport={LINK_PORT}\ndelay=2\ntimeout_ms=15000\n')
    (sd / 'no-scripted-input').write_text('')   # no smoke-build scripted presses
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / 'pc'
    pc.mkdir()
    (pc / 'group.ini').write_text('\n'.join(['role=host', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000',
                                             'test_control=1', 'test_character=1', '']) + '\n')
    control_file = pc / 'control.txt'
    control_file.write_text('')
    stub = Stub(emu.elf_symbols(args.elf))
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    t0 = time.monotonic()
    results = []
    pc_proc = None
    sent = [0]

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)
        return ok

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def pc_text():
        p = out / 'pc.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def wait_text(source, text, what, timeout=120, count=1):
        end = time.monotonic() + timeout
        while time.monotonic() < end and proc.poll() is None:
            if source().count(text) >= count:
                return check(what, True)
            time.sleep(0.5)
        return check(what, False, 'not seen: ' + text)

    def host(line):
        sent[0] += 1
        with open(control_file, 'a') as f:
            f.write(line + '\n')
        end = time.monotonic() + 30
        while time.monotonic() < end and f'control {sent[0]} done' not in pc_text():
            time.sleep(0.2)
        if f'control {sent[0]} done' not in pc_text():
            raise SystemExit(f'the PC host did not do: {line}')

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

    def eye(frames=40):
        """The camera's eye once it settled (it follows the stick at once; a few frames for the packets)."""
        stub.until_frames(frames)
        return struct.unpack('>fff', stub.peek('mp_test_camera_eye', 12))

    def read(addr, n):
        from gdb_probe import packet, receive

        def f(sock):
            packet(sock, f'm{addr:x},{n:x}')
            return bytes.fromhex(receive(sock))
        return stub._session(f)

    def plates():
        """Per door: (plate shown, name shown, the name's text)."""
        hidden_at = 0x4D                   # HSD_Text.hidden
        plate = struct.unpack('>4I', stub.peek('mp_css_plate', 16))
        text = struct.unpack('>4I', stub.peek('mp_css_dtext', 16))
        names = stub.peek('mp_css_name_now', 128)

        def shown(ptr):
            return ptr != 0 and read(ptr + hidden_at, 1)[0] == 0
        return [(shown(plate[d]), shown(text[d]),
                 names[32 * d:32 * d + 32].split(b'\0')[0]) for d in range(4)]

    def dist(p, q):
        return math.sqrt(sum((x - y) ** 2 for x, y in zip(p, q)))

    def pc_sticks():
        before = pc_text().count('group-test: sticks ')
        host('sticks')
        line = [l for l in pc_text().splitlines() if 'group-test: sticks ' in l][before]
        return [tuple(int(v) for v in pair.split(',')) for pair in line.split('group-test: sticks ')[1].split()]

    def camera_follows(where, me):
        rest = eye()
        host('stick 0 0')
        rest = eye()
        host('stick 120 0')
        right = eye()
        check(f'{where}: the PC console\'s C-stick turns this console\'s camera', dist(right, rest) > 1.0,
              f'rest {rest}, tilted {right}')
        host('stick 0 0')
        back = eye()
        check(f'{where}: the PC console\'s stick back to neutral puts the camera at rest', dist(back, rest) < 0.01,
              f'rest {rest}, now {back}')
        stub.poke('mp_test_keys', CSTICK_UP)
        own = eye()
        read = pc_sticks()
        stub.poke('mp_test_keys', 0)
        check(f'{where}: this console\'s own C-stick turns its camera', dist(own, rest) > 1.0, f'rest {rest}, tilted {own}')
        check(f'{where}: the PC console reads this console\'s C-stick', me >= 0 and read[me][1] > 100 and abs(read[me][0]) < 10,
              f'port {me + 1}: {read}')
        back = eye()
        check(f'{where}: own stick released: the camera at rest again', dist(back, rest) < 0.01, f'rest {rest}, now {back}')
        return rest

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        check('reach the main menu', to_main_menu(stub))
        s = stub.goto_vs_menu()
        check('reached the VS menu', s['scene'] == 1 and stub.menu()[0] == MENU_VS, str(stub.menu()))
        for _ in range(8):
            if stub.menu()[1] == 0:
                break
            stub.press(KEY_UP)
        stub.press(KEY_A)
        wait_text(log_text, 'group: the Solo/Group page is up', 'the Solo/Group page opens', 60)
        stub.choose()                  # GROUP
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the Host / Join page', 60)
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        if not wait_text(pc_text, 'group-test: in the lobby on port', 'the PC host opens its lobby', 60):
            raise SystemExit(1)
        host('door 2 1 10 3 1 0')        # a CPU, so the PC's Start can open the stage select later
        host('stick 0 120')              # held up before the emulated console arrives
        stub.until_frames(120)
        touch(160, 80)                   # the first game in the list (both layouts)
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] != 8:
            time.sleep(0.5)
        check('joined: on the lobby character select', stub.state()['scene'] == 8, str(stub.state()))
        arrived = eye(60)
        capture('01-arrived-tilted')
        host('stick 0 0')
        rest = eye()
        capture('02-rest')
        check('a tilt held before joining turns the camera as this console arrives', dist(arrived, rest) > 1.0,
              f'arrived {arrived}, rest {rest}')
        me = 1
        doors = plates()
        print('door plates:', doors, flush=True)
        check('a plate and name under the doors with a console (the PC host\'s and this one)',
              all(doors[d][0] and doors[d][1] and doors[d][2] for d in (0, me)), str(doors))
        check('nothing under the CPU\'s door and the empty one', all(not any(doors[d][:2]) for d in (2, 3)), str(doors))
        check('no signal bars under the doors (the bottom screen has them)', 'mp_css_bar' not in stub.sym)
        camera_follows('character select', me)

        # The stage select: the PC presses Start (its door and the CPU's are filled).
        host('start')
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] == 8:
            time.sleep(0.5)
        check('the PC\'s Start opens the stage select here', stub.state()['scene'] != 8, str(stub.state()))
        stub.until_frames(90)
        sss_rest = camera_follows('stage select', me)
        host('stick 0 120')
        capture('03-sss-tilted')
        check('stage select: a held tilt keeps the camera turned', dist(eye(), sss_rest) > 1.0)
        host('stick 0 0')
        host('back')
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] != 8:
            time.sleep(0.5)
        check('the PC\'s Back returns everyone to the character select', stub.state()['scene'] == 8, str(stub.state()))
        host('quit')
    finally:
        if pc_proc is not None and pc_proc.poll() is None:
            time.sleep(2)
            pc_proc.kill()
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
