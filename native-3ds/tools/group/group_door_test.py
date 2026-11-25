"""Doors in use: as on one GameCube, a door whose slider, name tag list, name entry or held token is in
use on one console cannot be switched between Human, CPU and closed from another, and the switch works again once it
is let go.

A PC console hosts (tools/group/src/group_client.c, test_control=1) with a level 3 CPU on door 3; the emulated 3DS joins.
Holding a slider or opening a name tag list by scripted input is impractical, so the test sets the CSS's own state
through the GDB stub (the door's is_hold_cpu_slider / is_hold_handicap_slider byte, the tag list's state byte): the CSS
then reports the door in use as it would for a real hold. Checked:
  1. the joiner holds door 3's CPU level slider: the lobby shows the door in use; the host's close of door 3 is refused
     (logged, the door stays a CPU on both consoles); let go, the same close is applied on both;
  2. the same with the handicap slider (the host's rules: handicap Fixed);
  3. the joiner's name tag list open on its own door: the host cannot close that door until the list is shut;
  4. the other way round: the host marks door 3 in use (its busy command with a door mask), and the joiner's own click
     on door 3's Human / CPU / closed switch does nothing; once the host lets go, the same click switches it.
Busy reasons: the strip says why a console is busy (mp_group_strip_text, each row's status as drawn): the joiner's own row
reads "CHOOSING A NAME TAG" while its list is open; the host's row "CHOOSING A NAME TAG", "SETTING THE RULES",
"ENTERING A NAME" and "ANSWERING A PROMPT" as the PC host sends those reasons. And this console's name comes from its Mii (no name= in its
group.ini): Azahar's FRD answers with the emulated console's user name.
Stale whole-door edits (the compare-and-set) are covered by lobby_unit (group_test.py runs it).

    docker/emu-test.sh run tools/group/group_door_test.py
Results: $MELEE_TEST_OUT/group-door/ (PNGs, game.log, the PC host's log)."""
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
from css_probe import CLOSED, CPU, DOOR_SIZE, HUMAN, Css  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402
from group_join_test import KEY_UP  # noqa: E402
from group_menu_test import KEY_A, MENU_VS, PORT, Stub, save_bottom, save_top, to_main_menu  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44100
CPU_DOOR = 2          # the host's CPU (door 3)
CPU_FIGHTER = 0x0A
HOLD_CPU, HOLD_HC = 0x12, 0x13   # CSSDoor.is_hold_cpu_slider / is_hold_handicap_slider
TAG_STATE = 0x1A                 # CSSTagData.state


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-door'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group-door', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text(f'menu=1\nudp=1\nport={LINK_PORT}\ndelay=2\ntimeout_ms=15000\n')
    (sd / 'no-scripted-input').write_text('')
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / 'pc'
    pc.mkdir()
    (pc / 'group.ini').write_text('\n'.join(['role=host', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000',
                                             'test_control=1', 'test_character=1', '']) + '\n')
    control_file = pc / 'control.txt'
    control_file.write_text('')
    stub = Stub(emu.elf_symbols(args.elf))
    css = Css(stub)
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
        stub.until_frames(30)

    def lobby():
        before = pc_text().count('group-test: lobby present')
        host('dump')
        line = [l for l in pc_text().splitlines() if 'group-test: lobby present' in l][before]
        words = line.split('group-test: lobby present ')[1].split()
        doors = {}
        for w in words[words.index('doors') + 1:]:
            i, v = w.split(':')
            doors[int(i)] = [int(x) for x in v.split('/')]
        return {'present': int(words[0], 16), 'busy': int(words[2], 16),
                'in_use': int(words[words.index('in_use') + 1], 16), 'doors': doors}

    def poke_bytes(addr, data):
        def f(s):
            packet(s, f'M{addr:x},{len(data):x}:' + data.hex())
            receive(s)
        stub._session(f)

    def read_bytes(addr, n):
        def f(s):
            packet(s, f'm{addr:x},{n:x}')
            return bytes.fromhex(receive(s))
        return stub._session(f)

    def door_addr(d, field):
        return stub.sym['mnCharSel_803F0DFC'] + d * DOOR_SIZE + field

    def tag_data(d):
        """The address of door d's CSSTagData (mnCharSel_803F0E8C[d].data; 12-byte CSSTag records)."""
        return struct.unpack('>I', stub.peek('mnCharSel_803F0E8C', 48)[12 * d:12 * d + 4])[0]

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

    def wait_lobby(pred, timeout=20):
        end = time.monotonic() + timeout
        lob = lobby()
        while time.monotonic() < end and not pred(lob):
            time.sleep(0.5)
            lob = lobby()
        return lob

    def refused_close(what, door, in_use_bit):
        """The host tries to close `door` while the joiner uses it: refused; then the joiner lets go (release()) and
        the same close is applied. Returns after the door is reopened as before."""
        lob = wait_lobby(lambda l: l['in_use'] >> in_use_bit & 1)
        check(f'{what}: the lobby shows door {door + 1} in use', lob['in_use'] >> in_use_bit & 1, str(lob))
        n = pc_text().count('kind change refused')
        before = lob['doors'][door][0]
        host(f'door {door} 2 0 3 0 0')
        lob = lobby()
        check(f'{what}: the host\'s close is refused while the joiner uses the door',
              lob['doors'][door][0] == before and pc_text().count('kind change refused') > n, str(lob['doors'][door]))
        kind = css.doors()[door]['p_kind']
        check(f'{what}: the door stays open on the joiner', kind != CLOSED, f'p_kind {kind}')

    def let_go_and_close(what, door):
        lob = wait_lobby(lambda l: not l['in_use'])
        check(f'{what}: let go, nothing is in use', lob['in_use'] == 0, str(lob))
        host(f'door {door} 2 0 3 0 0')
        lob = lobby()
        check(f'{what}: the same close is applied now', lob['doors'][door][0] == 2, str(lob['doors'][door]))
        stub.until_frames(30)
        kind = css.doors()[door]['p_kind']
        check(f'{what}: closed on the joiner too', kind == CLOSED, f'p_kind {kind}')

    def to_group_page():
        s = stub.goto_vs_menu()
        check('reached the VS menu', s['scene'] == 1 and stub.menu()[0] == MENU_VS, str(stub.menu()))
        for _ in range(8):
            if stub.menu()[1] == 0:
                break
            stub.press(KEY_UP)
        n = log_text().count('group: the Solo/Group page is up')
        stub.press(KEY_A)
        wait_text(log_text, 'group: the Solo/Group page is up', 'the Solo/Group page opens', 60, n + 1)
        n = log_text().count('group: Group, looking for games')
        stub.choose()
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the Host / Join page', 60, n + 1)

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        check('reach the main menu', to_main_menu(stub))
        to_group_page()
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        if not wait_text(pc_text, 'group-test: in the lobby on port', 'the PC host opens its lobby', 60):
            raise SystemExit(1)
        host(f'door {CPU_DOOR} 1 {CPU_FIGHTER} 3 1 0')
        stub.until_frames(120)
        touch(160, 80)                   # the first game in the list (both layouts)
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] != 8:
            time.sleep(0.5)
        check('joined the PC host\'s lobby (character select)', stub.state()['scene'] == 8)
        stub.until_frames(120)
        lob = lobby()
        me = [i for i in range(4) if lob['present'] >> i & 1 and i != 0]
        me = me[0] if me else -1
        check('the emulated console is in the lobby', me > 0 and lob['in_use'] == 0, str(lob))
        capture('01-joined')

        # 1. the CPU level slider of door 3 in the joiner's hand
        poke_bytes(door_addr(CPU_DOOR, HOLD_CPU), b'\x01')
        wait_text(log_text, f'GROUPCSS busy 1 why 1 doors {1 << CPU_DOOR:x}', 'level slider: the joiner reports door 3 in use', 60)
        refused_close('level slider', CPU_DOOR, CPU_DOOR)
        capture('02-level-slider-held')
        poke_bytes(door_addr(CPU_DOOR, HOLD_CPU), b'\x00')
        let_go_and_close('level slider', CPU_DOOR)
        capture('03-level-slider-let-go')

        # 2. the handicap slider (handicap Fixed)
        host('rules 2 0 3 4')
        host(f'door {CPU_DOOR} 1 {CPU_FIGHTER} 3 1 5')
        stub.until_frames(60)
        check('handicap slider: door 3 is a CPU again on the joiner', css.doors()[CPU_DOOR]['p_kind'] == CPU)
        poke_bytes(door_addr(CPU_DOOR, HOLD_HC), b'\x01')
        refused_close('handicap slider', CPU_DOOR, CPU_DOOR)
        poke_bytes(door_addr(CPU_DOOR, HOLD_HC), b'\x00')
        let_go_and_close('handicap slider', CPU_DOOR)
        host('rules 0 0 3 4')

        # 3. the joiner's name tag list open on its own door
        host(f'door {me} 0 5 3 1 0')     # its door open (a Human with a fighter), as its player would
        stub.until_frames(60)
        check('tag list: the joiner\'s door is Human', css.doors()[me]['p_kind'] == HUMAN, str(css.doors()[me]))
        tag = tag_data(me)
        poke_bytes(tag + TAG_STATE, b'\x01')   # what clicking its name plate does
        stub.until_frames(60)
        capture('04-tag-list-open')

        def strip_text(port):
            return stub.peek('mp_group_strip_text', 96)[24 * port:24 * port + 24].split(b'\0')[0].decode(errors='replace')
        check('tag list: the strip says "CHOOSING A NAME TAG" on the joiner\'s row', strip_text(me) == 'CHOOSING A NAME TAG',
              repr(strip_text(me)))
        refused_close('tag list', me, me)
        state = read_bytes(tag + TAG_STATE, 1)[0]
        if state != 0:
            poke_bytes(tag + TAG_STATE, b'\x04')   # what B does: the list closes
        end = time.monotonic() + 30
        while time.monotonic() < end and read_bytes(tag + TAG_STATE, 1)[0] != 0:
            stub.until_frames(10)
        let_go_and_close('tag list', me)

        # 4. the other way round: the host uses door 3, the joiner's own click on its switch does nothing
        host(f'door {CPU_DOOR} 1 {CPU_FIGHTER} 3 1 0')
        host(f'busy {1 | (1 << CPU_DOOR) << 4}')
        lob = wait_lobby(lambda l: l['in_use'] >> CPU_DOOR & 1)
        check('host uses door 3: the lobby shows it in use', lob['in_use'] >> CPU_DOOR & 1, str(lob))
        stub.until_frames(30)

        def click_switch(door):
            """Put the joiner's cursor on door's Human / CPU / closed switch and press A there."""
            ptr = struct.unpack('>I', stub.peek('mnCharSel_804A0BC0', 16)[4 * me:4 * me + 4])[0]
            left, right = struct.unpack('>ff', read_bytes(door_addr(door, 0x14), 8))
            x = (left + right) / 2
            for _ in range(3):           # the cursor is placed again each frame we hold it there
                poke_bytes(ptr + 0xC, struct.pack('>ff', x, -2.2))
                stub.until_frames(1)
            stub.poke('mp_test_keys', KEY_A)
            poke_bytes(ptr + 0xC, struct.pack('>ff', x, -2.2))
            stub.until_frames(2)
            stub.poke('mp_test_keys', 0)
            stub.until_frames(30)

        before = css.doors()[CPU_DOOR]['p_kind']
        click_switch(CPU_DOOR)
        after = css.doors()[CPU_DOOR]['p_kind']
        lob = lobby()
        capture('05-click-refused')
        check('the joiner\'s click on a door in the host\'s hands does nothing',
              before == CPU and after == CPU and lob['doors'][CPU_DOOR][0] == 1, f'p_kind {before} -> {after}, lobby {lob["doors"][CPU_DOOR]}')
        host('busy 0')
        wait_lobby(lambda l: not l['in_use'])
        stub.until_frames(30)
        click_switch(CPU_DOOR)
        after = css.doors()[CPU_DOOR]['p_kind']
        lob = wait_lobby(lambda l: l['doors'][CPU_DOOR][0] != 1, 10)
        capture('06-click-applied')
        check('once the host lets go, the same click switches the door (CPU -> closed)',
              after == CLOSED and lob['doors'][CPU_DOOR][0] == 2, f'p_kind {after}, lobby {lob["doors"][CPU_DOOR]}')
        # 5. the host's busy reasons on the joiner's strip (bits 0..3 of the busy command)
        for code, words in ((2, 'CHOOSING A NAME TAG'), (3, 'SETTING THE RULES'), (4, 'ENTERING A NAME'),
                            (6, 'ANSWERING A PROMPT')):
            host(f'busy {code}')
            end = time.monotonic() + 20
            while time.monotonic() < end and strip_text(0) != words:
                time.sleep(0.25)
            check(f'the host busy with reason {code}: its strip row reads "{words}"', strip_text(0) == words, repr(strip_text(0)))
        capture('07-host-entering-a-name')
        host('busy 0')
        end = time.monotonic() + 20
        while time.monotonic() < end and strip_text(0) in ('CHOOSING A NAME TAG', 'SETTING THE RULES', 'ENTERING A NAME',
                                                            'ANSWERING A PROMPT'):
            time.sleep(0.25)
        check('the host done: its row shows its state again', strip_text(0) not in ('CHOOSING A NAME TAG', 'SETTING THE RULES',
              'ENTERING A NAME', 'ANSWERING A PROMPT'), repr(strip_text(0)))
        # no name= here, so the Mii name (Azahar: the emulated user name)
        named = [l for l in log_text().splitlines() if 'group: name from ' in l]
        check('this console\'s name comes from its Mii', bool(named) and 'name from Mii' in named[0], str(named[:1]))
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
