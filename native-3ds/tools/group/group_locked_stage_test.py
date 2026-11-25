"""A stage or fighter this console has not unlocked, chosen on another console, in Azahar: the stage
select's pick of a locked stage, and a CPU's puck held over a locked fighter.

The emulated 3DS runs the fresh-save build (most stages locked); a PC console (tools/group/src/group_client.c,
test_control) hosts with everything available. The emulated console joins (Melee -> Group -> the first game), the PC
presses Start, and on the stage select the PC picks a stage. The stage select's state is read from memory
(mnStageSel_804D6CAE the selected icon, mnStageSel_804D6CAF the pick state, mnStageSel_803F06D0 the icon table).

Cases, each a fresh emulator run:
  locked-hover   a locked stage whose slot can be hovered here (x8 == 1): the selected icon goes to 0x1E ("nothing
                 selected": no outline, no name, an empty preview), "Now loading" plays, then the stage select ends
                 into the match (not back to the character select)
  locked-hidden  the same with a locked stage whose slot cannot be hovered (x8 == 0)
  unlocked       an unlocked stage: unchanged, the icon is selected (outline and name) and "Now loading" plays
  locked-puck    on the character select, the PC console holds its CPU's puck over a fighter this save has not
                 unlocked (the lobby door: CPU, held over that fighter): this console shows the puck as held (token
                 state 5) and sends nothing for that door, so the holder's puck stays in its hand; the same over an
                 unlocked fighter, then placed on the locked one. Before the fix this console read the puck as dropped
                 and sent that to the lobby, and the holder's puck fell out of its hand.

    docker/emu-test.sh run tools/group/group_locked_stage_test.py [--case NAME] [--show]
Results: $MELEE_TEST_OUT/group-locked-stage/ (PNGs per case, game.log, the PC console's log)."""
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
from group_menu_test import KEY_A, KEY_UP, MENU_VS, PORT, Stub, save_bottom, save_top, to_main_menu  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
LINK_PORT = 44100
SCENE_CSS = 8
NOTHING = 0x1E                  # the stage select's "nothing selected" index
ENTRY, ENTRIES = 0x1C, 30       # mnStageSel_803F06D0: x0 jobj, x4, x8 unlocked state, x9, xA, xB stage kind, ...
CASES = ('locked-hover', 'locked-hidden', 'unlocked', 'locked-puck')
ICON, ICONS = 0x1C, 25          # mncharsel.c icons[]: x1 fighter (CKind), x2 state (0 locked)
MARTH = 9


def default_dsx():
    d = os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx')
    return d.replace('melee-development.3dsx', 'melee-fresh-development.3dsx')


def default_elf():
    e = os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf')
    return e.replace('/game-opt/', '/game-opt-fresh/')


def run_case(case, args, out, results):
    def check(name, ok, detail=''):
        results.append((f'{case}: {name}', ok))
        print(('PASS ' if ok else 'FAIL ') + f'{case}: {name}', detail, flush=True)
        return ok

    home = emu.prepare('group-locked-stage', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text(f'menu=1\nudp=1\nport={LINK_PORT}\ndelay=2\ntimeout_ms=15000\n')
    (sd / 'no-scripted-input').write_text('')
    (sd / 'game.log').unlink(missing_ok=True)
    pc = out / f'pc-{case}'
    shutil.rmtree(pc, ignore_errors=True)
    pc.mkdir()
    (pc / 'group.ini').write_text('\n'.join(['role=host', 'udp=1', f'port={LINK_PORT}', 'delay=2', 'timeout_ms=15000',
                                             'test_control=1', 'test_character=1', '']) + '\n')
    control_file = pc / 'control.txt'
    control_file.write_text('')
    pc_log_path = out / f'pc-{case}.log'
    stub = Stub(emu.elf_symbols(args.elf))
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    pc_proc = None
    sent = [0]

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def pc_text():
        return pc_log_path.read_text(errors='replace') if pc_log_path.exists() else ''

    def wait_text(source, text, what, timeout=120):
        end = time.monotonic() + timeout
        while time.monotonic() < end and proc.poll() is None:
            if text in source():
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
            time.sleep(0.25)
            if int.from_bytes(stub.peek('mp_test_capture', 4), 'little') == 0:
                break
        save_top(sd / 'engine-top.bgr', out / f'{case}-{name}-top.png')
        save_bottom(sd / 'engine-bottom.rgb565', out / f'{case}-{name}-bottom.png')

    def byte(sym):
        return stub.peek(sym, 1)[0]

    def read(addr, n):
        from gdb_probe import packet, receive

        def f(sock):
            packet(sock, f'm{addr:x},{n:x}')
            return bytes.fromhex(receive(sock))
        return stub._session(f)

    def token_state(d):
        ptr = int.from_bytes(stub.peek('mnCharSel_804A0BD0', 16)[4 * d:4 * d + 4], 'big')
        return read(ptr + 5, 1)[0] if ptr else -1

    def lobby_door(d):
        before = pc_text().count('group-test: lobby present')
        host('dump')
        line = [l for l in pc_text().splitlines() if 'group-test: lobby present' in l][before]
        kind, fighter, level, chosen, hc = (int(v) for v in line.split(f' {d}:')[1].split()[0].split('/'))
        return kind, fighter, chosen

    def puck_case():
        raw = stub.peek('icons', ICON * ICONS)
        icons = [(raw[i * ICON + 1], raw[i * ICON + 2]) for i in range(ICONS)]
        print('fighter icons (fighter, state):', icons, flush=True)
        locked = [ck for ck, st in icons if st == 0]
        free = [ck for ck, st in icons if st >= 1]
        if not check('the fresh save has locked fighters (Marth among them)', MARTH in locked and bool(free), str(icons)):
            raise SystemExit(1)
        host('busy 64')                  # door 3 (index 2) in the PC console's hands, as a held puck marks it
        for what, ck, chosen in (('held over a locked fighter', MARTH, 2), ('held over an unlocked fighter', free[0], 2),
                                 ('placed on the locked fighter', MARTH, 1)):
            mark = len(log_text())
            host(f'door 2 1 {ck} 3 {chosen} 0')
            stub.until_frames(120)
            sent_here = [l for l in log_text()[mark:].splitlines() if 'GROUPCSS send door 2' in l]
            check(f'{what}: this console sends nothing for that door', not sent_here, str(sent_here))
            tok = token_state(2)
            check(f'{what}: the token is ' + ('held elsewhere here (state 5)' if chosen == 2 else 'placed (state 0)'),
                  tok == (5 if chosen == 2 else 0), f'token state {tok}')
            door = lobby_door(2)
            check(f'{what}: the lobby still has the door as the holder set it', door == (1, ck, chosen), str(door))
            if ck == MARTH and chosen == 2:
                capture('01-held-over-locked')
        host('busy 0')

    def table():
        raw = stub.peek('mnStageSel_803F06D0', ENTRY * ENTRIES)
        return [(i, raw[i * ENTRY + 8], raw[i * ENTRY + 0xB]) for i in range(ENTRIES)]   # (slot, x8, stage kind)

    try:
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        check('reach the main menu', to_main_menu(stub))
        stub.goto_vs_menu()
        for _ in range(8):
            if stub.menu()[1] == 0:
                break
            stub.press(KEY_UP)
        stub.press(KEY_A)
        wait_text(log_text, 'group: the Solo/Group page is up', 'the Solo/Group page opens', 60)
        touch(238, 104)                  # GROUP
        wait_text(log_text, 'group: Group, looking for games', 'Group opens the Host / Join page', 60)
        pc_log = open(pc_log_path, 'w')
        pc_proc = subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(pc)], stdout=pc_log, stderr=subprocess.STDOUT)
        if not wait_text(pc_text, 'group-test: in the lobby on port', 'the PC host opens its lobby', 60):
            raise SystemExit(1)
        host('door 2 1 10 3 1 0')        # a CPU, so the PC's Start opens the stage select
        stub.until_frames(120)
        touch(160, 80)                   # the first game in the list
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] != SCENE_CSS:
            time.sleep(0.5)
        check('joined: on the lobby character select', stub.state()['scene'] == SCENE_CSS, str(stub.state()))
        stub.until_frames(60)
        if case == 'locked-puck':
            puck_case()
            host('quit')
            return
        host('start')
        end = time.monotonic() + 120
        while time.monotonic() < end and stub.state()['scene'] == SCENE_CSS:
            time.sleep(0.5)
        sss_scene = stub.state()['scene']
        check('the PC\'s Start opens the stage select here', sss_scene != SCENE_CSS, str(stub.state()))
        stub.until_frames(120)           # the stage select's intro and its first hover
        icons = table()
        print('stage icons (slot, x8, kind):', icons, flush=True)
        locked = [t for t in icons[:29] if t[1] < 2 and t[2]]
        check('the fresh save has locked stages here', bool(locked), str(icons))
        want = {'locked-hover': 1, 'locked-hidden': 0}.get(case)
        pick = [t for t in icons[:29] if (t[1] == want if want is not None else t[1] >= 2) and t[2]]
        if not check(f'a stage icon for this case (x8 {"== %d" % want if want is not None else ">= 2"})', bool(pick), str(icons)):
            raise SystemExit(1)
        slot, x8, stage = pick[0] if case != 'unlocked' else pick[len(pick) // 2]
        print(f'picking slot {slot} (x8 {x8}): stage kind {stage}', flush=True)
        capture('01-before')
        before_log = len(log_text())
        frames0 = stub.frames()
        sent[0] += 1                     # the pick runs while this console is polled: no wait for the PC's echo here
        with open(control_file, 'a') as f:
            f.write(f'stage {stage}\n')

        # Poll the pick: the selected icon and the pick state while the stage select is up.
        seen_sel, loading_from, left_at, captured = set(), None, None, False
        end = time.monotonic() + 60
        while time.monotonic() < end and proc.poll() is None:
            scene = stub.state()['scene']
            if scene != sss_scene:
                left_at = stub.frames()
                break
            caf, cae = byte('mnStageSel_804D6CAF'), byte('mnStageSel_804D6CAE')
            if caf != 0:
                seen_sel.add(cae)
                if loading_from is None:
                    loading_from = stub.frames()
                if not captured:
                    stub.until_frames(8)  # the overlay's first frames are drawn a frame or two later
                    capture('02-now-loading')
                    captured = True
            time.sleep(0.03)
        after = stub.state()
        log = log_text()[before_log:]
        print('selected icons during the pick:', sorted(seen_sel), '; pick from frame', loading_from, 'left at', left_at,
              '; scene now', after['scene'], flush=True)
        check('this console shows the pick ("Now loading": the pick state is set)', loading_from is not None)
        check('the stage select ends', left_at is not None, str(after))
        check('into the match, not back to the character select', after['scene'] not in (SCENE_CSS, sss_scene), str(after))
        if loading_from is not None and left_at is not None:
            check('"Now loading" lasts about half a second or more (30 frames; polled)', left_at - loading_from >= 20,
                  f'{left_at - loading_from} frames')
        if case == 'unlocked':
            check('the picked stage\'s icon stays selected (outline and name)', seen_sel == {slot}, str(sorted(seen_sel)))
            check('no "locked here" log line', 'is locked here' not in log)
        else:
            check('nothing is selected during "Now loading" (no outline, name or preview)', seen_sel == {NOTHING},
                  str(sorted(seen_sel)))
            check('the log says the stage is locked here', f'stage {stage} is locked here' in log)
        stub.until_frames(60)
        capture('03-after')
        check('the match is set up with the picked stage', stub.state()['stage'] == stage, str(stub.state()))
        print(f'{case}: {stub.frames() - frames0} frames since the pick', flush=True)
        host('quit')
    finally:
        if pc_proc is not None and pc_proc.poll() is None:
            time.sleep(2)
            pc_proc.kill()
        emu.stop(proc)
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / f'game-{case}.log')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=default_dsx())
    ap.add_argument('--elf', default=default_elf())
    ap.add_argument('--case', choices=CASES, action='append', help='run only these cases (default: all)')
    ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'group-locked-stage'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    t0 = time.monotonic()
    results = []
    for case in args.case or CASES:
        try:
            run_case(case, args, out, results)
        except SystemExit as e:
            results.append((f'{case}: ran to the end', False))
            print(f'FAIL {case}: stopped: {e}', flush=True)
    print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
