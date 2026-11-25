"""The emulated 3DS joins a PC console's lobby, and the test changes that lobby from the host's side while it watches
the emulated console's character select (its memory, not only its log).

The PC host (tools/group/src/group_client.c, test_control=1) takes its door and a level 7 CPU before the emulated console
joins, then does what this test appends to its control file. Checked on the emulated joiner:
  * joining opens no door: its own door stays closed and it sends none; its banner takes the host's rules;
  * a CPU level set before it joined shows on its CPU door's level slider (the knob), and a later change follows;
  * READY shows with two fighters in, and hides while the host is busy (a name tag list, the rules menu, a slider);
  * the host's handicap mode: Off -> Fixed places the CPU door's level and handicap sliders at their values (and the
    joiner does not send its own saved handicap over the CPU's), the closed doors do not play their closing animation
    again, and back to Off the level slider is right again;
  * Back leaves; a rejoin over a door the host made a CPU keeps that CPU (the old door is never sent back).
  * the host quits: back on "Choose a game." a top-screen dialog says why (one button, OK), the Group list open under
    it at once; OK closes the dialog.
Screenshots of each step.

    docker/emu-test.sh run tools/group/group_join_test.py
Results: $MELEE_TEST_OUT/group-join/ (PNGs, game.log, the PC host's log)."""
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
from css_probe import CLOSED, CPU, Css, knob_x  # noqa: E402
from group_menu_test import to_main_menu, KEY_A, KEY_B, MENU_VS, PORT, SCENE_CHOOSE, Stub, save_bottom, save_top  # noqa: E402

KEY_UP = 0x40
TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44100
CPU_PORT = 2          # the host's CPU (door 3); the emulated console takes the first free port, door 2
CPU_FIGHTER = 0x0A


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-join'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group-join', PORT)
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
        """One line to the PC host; returns once it says it is done (and the lobby has had time to arrive)."""
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
        """The host's view of the lobby (a dump line): present mask, busy, and door kind/fighter/level/chosen/handicap."""
        before = pc_text().count('group-test: lobby present')
        host('dump')
        line = [l for l in pc_text().splitlines() if 'group-test: lobby present' in l][before]
        words = line.split('group-test: lobby present ')[1].split()
        doors = {}
        for w in words[words.index('doors') + 1:]:
            i, v = w.split(':')
            doors[int(i)] = [int(x) for x in v.split('/')]
        return {'present': int(words[0], 16), 'busy': int(words[2], 16), 'doors': doors}

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

    def near(a, b):
        return a is not None and abs(a - b) < 0.05

    def ready_for(frames=40):
        """READY's counter over some frames: (any frame it showed, any frame it was hidden)."""
        shown = hidden = False
        start = stub.frames()
        while stub.frames() - start < frames:
            if css.ready():
                shown = True
            else:
                hidden = True
        return shown, hidden

    def closed_doors_quiet(doors, frames=60):
        """No closed door starts its closing animation (dooranim_timer set to 0x1E by the door redraw) for a while."""
        seen = []
        start = stub.frames()
        while stub.frames() - start < frames:
            for i, d in enumerate(css.doors()):
                if i in doors and d['dooranim_timer']:
                    seen.append((i, d['dooranim_timer'], d['p_kind']))
        return seen

    def to_group_page(lists=None):
        stub.until_frames(30)
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] not in (1, SCENE_CHOOSE):
            time.sleep(0.5)
        if lists is not None and stub.state()['scene'] == SCENE_CHOOSE:
            wait_text(log_text, 'group: Group, looking for games',
                      'back from the lobby: on "Choose a game." with the Group list open again', 60, lists + 1)
            return
        if stub.menu()[0] == 11:
            check('back from the lobby: on the Solo / Group submenu, Group hovered', stub.menu() == (11, 1),
                  str(stub.menu()))
        else:
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
        stub.choose()                  # GROUP
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the Host / Join page', 60, n + 1)

    def join(what):
        stub.until_frames(120)           # a few scans, so the game is in the list
        touch(160, 80)                   # the first game in the list (both layouts)
        # The character select is the sign of a join (the native side's "joined" log line can reach game.log late).
        end = time.monotonic() + 120
        s = None
        while time.monotonic() < end:
            s = stub.state()
            if s['scene'] == 8:
                break
            time.sleep(0.5)
        check(what + ': on the lobby character select', s is not None and s['scene'] == 8 and s['mode'] == 8, str(s))
        stub.until_frames(120)

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        check('reach the main menu', to_main_menu(stub))
        to_group_page()
        pc_log = open(out / 'pc.log', 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        if not wait_text(pc_text, 'group-test: in the lobby on port', 'the PC host opens its lobby', 60):
            raise SystemExit(1)
        # Before anyone joins: a level 7 CPU on door 3, the host's rules with handicap off.
        host(f'door {CPU_PORT} 1 {CPU_FIGHTER} 7 1 0')
        host('rules 0 0 3 4')            # a 3 minute time match: the banner here must say so (this save's own is stock)
        join('joined the PC host\'s lobby')
        mark = len(log_text())
        lob = lobby()
        me = [i for i in range(4) if lob['present'] >> i & 1 and i != 0]
        me = me[0] if me else -1
        check('the emulated console is in the lobby', me > 0, str(lob))
        capture('01-joined')
        wait_text(log_text, "GROUPCSS the host's rules: mode 0 time 3", 'the joiner\'s banner takes the host\'s rules (time, 3 min)', 60)

        # Joining opens no door.
        doors = css.doors()
        check('joining leaves this console\'s door closed', doors[me]['p_kind'] == CLOSED and lob['doors'][me][0] == 2,
              f"CSS p_kind {doors[me]['p_kind']}, lobby {lob['doors'][me]}")
        check('the joiner sends no door of its own on arrival', f'GROUPCSS send door {me}' not in log_text()[mark:],
              '; '.join(l for l in log_text()[mark:].splitlines() if 'send door' in l)[:300])

        # The CPU level set before joining shows on the slider.
        check('the CPU door is a CPU here', doors[CPU_PORT]['p_kind'] == CPU, str(doors[CPU_PORT]))
        x = css.knob(CPU_PORT, 'cpuslider_joint')
        check('the CPU level set before joining shows on its slider (level 7)', near(x, knob_x(7)), f'knob x {x}')
        host(f'door {CPU_PORT} 1 {CPU_FIGHTER} 2 1 0')
        x = css.knob(CPU_PORT, 'cpuslider_joint')
        check('a CPU level changed on the host follows on the slider (level 2)', near(x, knob_x(2)), f'knob x {x}')
        capture('02-cpu-level')

        # READY: two fighters in (the host's and the CPU), so it shows; the host busy hides it.
        shown, _ = ready_for()
        check('READY shows with the host and a CPU in', shown)
        host('busy 1')
        shown, _ = ready_for()
        check('READY hides while the host is busy', not shown)
        capture('03-host-busy')
        host('busy 0')
        shown, _ = ready_for()
        check('READY comes back when the host is done', shown)

        # Handicap Off -> Fixed: the sliders take their values; closed doors keep still.
        hc_mark = len(log_text())
        closed = [i for i, d in enumerate(css.doors()) if d['p_kind'] == CLOSED]
        with open(control_file, 'a') as f:     # rules and the CPU's handicap together, then watch at once
            f.write(f'rules 2 0 3 4\ndoor {CPU_PORT} 1 {CPU_FIGHTER} 2 1 4\n')
        sent[0] += 2
        moved = closed_doors_quiet(closed)
        check('a handicap mode change does not replay the closed doors\' animation', not moved, f'closed {closed}: {moved[:6]}')
        stub.until_frames(30)
        lv = css.knob(CPU_PORT, 'cpuslider2_joint')
        hc = css.knob(CPU_PORT, 'cpuslider_joint')
        check('handicap on: the CPU level slider shows level 2', near(lv, knob_x(2)), f'knob x {lv}')
        check('handicap on: the handicap slider shows 4', near(hc, knob_x(4)), f'knob x {hc}')
        capture('04-handicap-fixed')
        closed = [i for i, d in enumerate(css.doors()) if d['p_kind'] == CLOSED]
        with open(control_file, 'a') as f:
            f.write('rules 0 0 3 4\n')
        sent[0] += 1
        moved = closed_doors_quiet(closed)
        check('handicap back off: the closed doors keep still', not moved, f'closed {closed}: {moved[:6]}')
        stub.until_frames(30)
        x = css.knob(CPU_PORT, 'cpuslider_joint')
        check('handicap off again: the CPU level slider shows level 2', near(x, knob_x(2)), f'knob x {x}')
        capture('05-handicap-off')
        stub.until_frames(60)
        sends = [l for l in log_text()[hc_mark:].splitlines() if f'GROUPCSS send door {CPU_PORT}' in l]
        check('the handicap mode changes send no CPU door from the joiner', not sends, '; '.join(sends)[:300])

        # Back leaves (to "Choose a game.", the list open); the host makes this console's door a CPU; a rejoin keeps it.
        lists = log_text().count('group: Group, looking for games')
        stub.press(KEY_B, 2, 4)
        stub.poke('mp_test_keys', KEY_B)
        start, end = stub.frames(), time.monotonic() + 120
        while time.monotonic() < end and stub.peek('mnCharSel_804D6CF6', 1)[0] != 2 and stub.frames() - start < 120:
            time.sleep(0.05)
        stub.poke('mp_test_keys', 0)
        wait_text(log_text, 'group: left the lobby', 'Back leaves the lobby', 60)
        end = time.monotonic() + 60
        while time.monotonic() < end and lobby()['present'] >> me & 1:
            time.sleep(1)
        host(f'door {me} 1 5 5 1 0')   # what leaving does to a Human door, done by hand: this door was closed
        to_group_page(lists)
        mark = len(log_text())
        join('rejoined the PC host\'s lobby')
        lob = lobby()
        me2 = [i for i in range(4) if lob['present'] >> i & 1 and i != 0]
        me2 = me2[0] if me2 else -1
        capture('06-rejoined')
        doors = css.doors()
        if me2 == me:
            check('a rejoin over the CPU the host put on this port keeps it', doors[me]['p_kind'] == CPU and lob['doors'][me][0] == 1,
                  f"CSS p_kind {doors[me]['p_kind']}, lobby {lob['doors'][me]}")
        else:
            check('a rejoin lands on a free port with its door closed', me2 > 0 and doors[me2]['p_kind'] == CLOSED,
                  f'port {me2}, CSS {doors[me2] if me2 >= 0 else None}, lobby {lob}')
        check('the rejoin sends no door of its own', f'GROUPCSS send door {me2}' not in log_text()[mark:],
              '; '.join(l for l in log_text()[mark:].splitlines() if 'send door' in l)[:300])
        # The host goes away: back on "Choose a game." (the VS menu without the menu art) a dialog says why, the Group
        # list open under it at once (with the submenu); OK closes the dialog.
        mark = len(log_text())
        n = log_text().count('group: Group, looking for games')
        host('quit')
        ok = wait_text(lambda: log_text()[mark:], 'group: dialog "The host', 'the host leaving: a dialog says why', 120)
        stub.until_frames(40)
        capture('07-host-left-dialog')
        top = 'group: menu art filled' in log_text()
        s = stub.state()
        check('the notice is over ' + ('"Choose a game."' if top else 'the VS menu'),
              s['scene'] == SCENE_CHOOSE if top else stub.menu() == (MENU_VS, 0), f'{s}, menu {stub.menu()}')
        note = [l for l in log_text()[mark:].splitlines() if 'group: dialog "' in l]
        check('the notice has one button (OK)', bool(note) and note[0].endswith('[OK]'), str(note[:1]))
        if top:
            wait_text(log_text, 'group: Group, looking for games', 'the Group list is open under the notice', 60, n + 1)
        else:
            check('the Host / Join page waits for the notice', log_text().count('group: Group, looking for games') == n)
        if ok:
            stub.press(KEY_A, 2, 30)
            if not top:
                wait_text(log_text, 'group: Group, looking for games', 'OK: the Host / Join page opens again', 60, n + 1)
            stub.until_frames(40)
            capture('08-after-ok')
            s = stub.state()
            check('OK: still on ' + ('"Choose a game.", the list open' if top else 'the VS menu'),
                  (s['scene'] == SCENE_CHOOSE and log_text().count('group: Group, looking for games') == n + 1) if top
                  else stub.menu() == (MENU_VS, 0), f'{s}, menu {stub.menu()}')
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
