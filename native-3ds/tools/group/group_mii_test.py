"""The Group list's Mii heads and pages in Azahar: five PC hosts (tools/group/src/group_client.c) over
the loopback link, started one after another so the list order is known:

    Dana    a Mii (tools/group/test_miis.py)          -> a head
    Eli     test_mii_fail=1 (no Mii)                  -> the emblem
    Fumiko  test_mii_corrupt=1 (bad CRC)              -> the emblem
    Gus     a Mii                                     -> a head
    Hana    a Mii                                     -> a head

The emulated console browses (Melee -> Group), three games a page. It must list all five over two pages,
render heads only for the rows of the page shown and this console's own on the "HOST GAME" button (the game's renderer runs cfl-mii between its frames: "mii: icon ..."
lines), draw the emblem for the other two, turn the page by touch and render the heads there, and unload cfl-mii once
the list is left. Each row's head is read from mp_group_row_head (1 head, 2 emblem, 0 no row); screenshots of both
screens at each step (list-*-bottom.png). With MELEE_CFL_RES (docker/emu-test.sh) the heads have real faces.

Two profiles: by default the console has a Mii and a Mii Maker database (a generated CFL_DB.dat holding its
own Mii, test_miis.cfl_db): its own head shows on the "HOST GAME" button, and cfl-mii adds each other console's Mii it
draws to the database's recently-seen list (checked in the file afterwards: three, and nothing printed by cfl-mii).
With --no-mii: test_mii_fail=1 and no database: no head on the button, the rows' heads as before.

MELEE_CFL_DB (docker/emu-test.sh) is a folder with a console's own Mii files from shared extdata F000000B
(CFL_DB.dat, and CFL_OldDB.dat and ubll.lst if there): they are copied into the profile instead of the generated
database, so cfl-mii's database work costs what it costs on that console. The folder itself is never written. A real
recently-seen list is usually full, so the check becomes: every other console's Mii drawn is in it afterwards.

    docker/emu-test.sh run tools/group/group_mii_test.py [--no-mii]
Results: $MELEE_TEST_OUT/group-mii[-no-mii]/ (PNGs, game.log, the PC hosts' logs)."""
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
import test_miis  # noqa: E402
from group_menu_test import KEY_A, KEY_START, MENU_VS, PORT, Stub, save_bottom, save_top  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or emu.ROOT / 'build/group-tools')
HOSTS = [('Dana', 'mii'), ('Eli', 'fail'), ('Fumiko', 'corrupt'), ('Gus', 'mii'), ('Hana', 'mii')]
WANT_HEAD = {'mii': 1, 'fail': 2, 'corrupt': 2}


def save_burst(src, out):
    """The burst frames as PNGs, and a contact sheet of every frame that differs from the one before (with its number)."""
    from group_menu_test import png
    dest = out / 'burst'
    dest.mkdir(exist_ok=True)
    frames = sorted(src.glob('*.rgb565'))
    changed, last = [], None
    for f in frames:
        d = f.read_bytes()
        if len(d) != 320 * 240 * 2:
            continue
        save_bottom(f, dest / (f.stem + '.png'))
        if last is None or sum(1 for i in range(0, len(d), 64) if d[i:i + 2] != last[i:i + 2]) > 20:
            changed.append((f.stem, d))
        last = d
    print(f'burst: {len(frames)} frames, {len(changed)} changed', flush=True)
    (dest / 'changed.txt').write_text(''.join(n + '\n' for n, _ in changed))
    # contact sheet: 4 columns of half-size frames
    cols, tw, th = 4, 160, 120
    shown = changed[:48]
    rows_n = (len(shown) + cols - 1) // cols
    sheet = [bytearray(cols * tw * 3) for _ in range(max(1, rows_n) * th)]
    for k, (_, d) in enumerate(shown):
        ox, oy = (k % cols) * tw, (k // cols) * th
        for y in range(th):
            row = sheet[oy + y]
            for x in range(tw):
                v = d[((2 * x) * 240 + (239 - 2 * y)) * 2] | d[((2 * x) * 240 + (239 - 2 * y)) * 2 + 1] << 8
                row[(ox + x) * 3:(ox + x) * 3 + 3] = bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    png(out / 'burst-changes.png', cols * tw, max(1, rows_n) * th, sheet)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--no-mii', action='store_true', help='the profile without a Mii: test_mii_fail=1, no CFL_DB.dat')
    ap.add_argument('--burst', type=int, default=0, help='capture the displayed bottom screen every frame from the Group tap '
                    'through the page turn (at most N frames): burst/NNNN.png and burst-changes.png, the frames that changed')
    args = ap.parse_args()
    rows = 3
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / ('group-mii' + ('-no-mii' if args.no_mii else ''))
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare('group', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('prefetch=0\ndelay=2\n')
    (sd / 'slippi' / 'group.ini').write_text('menu=1\nudp=1\nport=44100\ndelay=2\n' + ('test_mii_fail=1\n' if args.no_mii else ''))
    # the Mii Maker database (shared extdata F000000B), made on every run so a reset cannot change it
    db_path = emu.user_dir(home) / 'nand/data/00000000000000000000000000000000/extdata/00048000/F000000B/user/CFL_DB.dat'
    shutil.rmtree(db_path.parent.parent, ignore_errors=True)
    real_db = os.environ.get('MELEE_CFL_DB_DIR')
    if not args.no_mii:
        db_path.parent.mkdir(parents=True, exist_ok=True)
        if real_db:
            copied = []
            for name in ('CFL_DB.dat', 'CFL_OldDB.dat', 'ubll.lst'):
                if (Path(real_db) / name).is_file():
                    shutil.copyfile(Path(real_db) / name, db_path.parent / name)
                    copied.append(name)
            if 'CFL_DB.dat' not in copied:
                sys.exit(f'MELEE_CFL_DB: no CFL_DB.dat in {real_db}')
            print('the console\'s Mii files:', ', '.join(copied), flush=True)
        else:
            db_path.write_bytes(test_miis.cfl_db())
    (sd / 'no-scripted-input').write_text('')
    (sd / 'game.log').unlink(missing_ok=True)
    stub = Stub(emu.elf_symbols(args.elf))
    results, procs = [], []
    t0 = time.monotonic()

    def check(name, ok, detail=''):
        results.append((name, ok))
        print(('PASS ' if ok else 'FAIL ') + name, detail, flush=True)

    def log_text():
        p = sd / 'game.log'
        return p.read_text(errors='replace') if p.exists() else ''

    def wait_log(pattern, count=1, timeout=120):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if len(re.findall(pattern, log_text())) >= count:
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
        print('captured', name, flush=True)

    def heads():
        return list(stub.peek('mp_group_row_head', 4))

    def wait_heads(want, timeout=120):
        end, got = time.monotonic() + timeout, None
        while time.monotonic() < end:
            got = heads()
            if got[:rows] == want:
                return got
            time.sleep(1)
        return got

    proc = None
    try:
        miis = dict(test_miis.hosts())
        for name, kind in HOSTS:               # one after another: the list keeps the order games were found in
            d = out / f'pc-{name}'
            d.mkdir()
            lines = ['role=host', 'udp=1', 'port=44100', 'delay=2', f'name={name}', 'test_players=9', 'test_control=1',
                     f'test_mii={miis.get(name, test_miis.hosts()[0][1])}']
            if kind == 'fail':
                lines.append('test_mii_fail=1')
            if kind == 'corrupt':
                lines.append('test_mii_corrupt=1')
            (d / 'group.ini').write_text('\n'.join(lines) + '\n')
            (d / 'control.txt').write_text('door 1 1 9 3 1 0\n')
            procs.append(subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(d)], stdout=open(d / 'stdout.txt', 'wb'),
                                          stderr=subprocess.STDOUT))
            time.sleep(1.0)
        proc = emu.launch(home, Path(args.dsx).resolve(), hidden=not args.show)
        time.sleep(15)
        stub.poke('mp_test_frame_limit', 0)
        end = time.monotonic() + 360
        while stub.state()['scene'] != 1 and time.monotonic() < end:
            stub.press(KEY_START | KEY_A, 2, 30)
        s = stub.goto_vs_menu()
        check('reached the VS menu', stub.menu()[0] == MENU_VS, str(s))
        stub.press(KEY_A)
        wait_log('group: the Solo/Group page is up')
        if args.burst:
            (sd / 'burst').mkdir(exist_ok=True)
            stub.poke('mp_test_burst', args.burst)
        stub.choose()                         # GROUP
        ok = wait_log(r'group: 5 games listed', 1, 120)
        check('the five PC hosts are listed', ok)
        # the list's order (the order the hosts were found in) and which carried a Mii, as the game logged it
        time.sleep(2)
        order = re.findall(r'group: list order:(.*)', log_text())[-1].split()
        kinds = dict(HOSTS)
        names = [o.split('(')[0] for o in order]
        check('each host is listed with a Mii or without as it sent one (no Mii, a corrupt one: without)',
              all(('(Mii)' in o) == (kinds.get(o.split('(')[0]) == 'mii') for o in order) and len(order) == 5, str(order))
        listed = [kinds.get(n, 'fail') for n in names]
        # page 1: the heads of its rows, the emblem for no Mii and a corrupt one
        want = [WANT_HEAD[k] for k in listed[:rows]]
        got = wait_heads(want)
        capture('list-page-1')
        check(f'page 1 heads {want} (1 head, 2 emblem)', got[:rows] == want, str(got))
        n_icons = len(re.findall(r'mii: icon for game \S+ crc \S+ ok', log_text()))
        own = 0 if args.no_mii else 1
        check('page 1 rendered one icon per row with a Mii' + ('' if args.no_mii else ', and this console\'s own for the Host button'),
              n_icons == want.count(1) + own, f'{n_icons} icons')
        if args.no_mii:
            check('no Mii (test_mii_fail): no head on the Host button', stub.peek('mp_group_host_head', 1)[0] == 0)
        else:
            check('the Host button shows this console\'s own Mii head (Azahar\'s built-in Mii)', stub.peek('mp_group_host_head', 1)[0] == 1)
        check('cfl-mii loaded on the renderer thread', 'mii: CFL_Initialize ok' in log_text())
        check('the corrupt Mii never reached the renderer (the browser turned it down)',
              n_icons == want.count(1) + own and 'FAILED' not in ''.join(l for l in log_text().splitlines() if 'mii:' in l))
        # page 2 by touch (the > button beside "1/2")
        touch(242, 48)
        ok = wait_log(r'group: list page 2 of 2')
        rest = [WANT_HEAD[k] for k in listed[rows:]]
        rest += [0] * (rows - len(rest))
        got = wait_heads(rest)
        capture('list-page-2')
        check(f'the > button turns to page 2; heads {rest}', ok and got[:rows] == rest, str(got))
        if args.burst:
            stub.until_frames(60)
            stub.poke('mp_test_burst', 0)
            save_burst(sd / 'burst', out)
        touch(178, 48)
        ok = wait_log(r'group: list page 1 of 2')
        got = wait_heads(want, 30)
        check('the < button turns back; the cached heads draw again', ok and got[:rows] == want, str(got))
        capture('list-page-1-again')
        total = len(re.findall(r'mii: icon for game \S+ crc \S+ ok', log_text()))
        check('heads are cached (no render for a page seen before)', total == want.count(1) + rest.count(1) + own, f'{total} icons')
        costs = [int(m) for m in re.findall(r'mii: icon for game .* all (\d+) ms', log_text())]
        check('each icon job is logged with its cost', len(costs) == total, f'all ms: {costs}')
        # leave the list: cfl-mii is let go after 2 s without a request
        touch(286, 48)                          # BACK (layout with pages: x 262..312)
        ok = wait_log(r'mii: cfl-mii unloaded', 1, 60)
        check('cfl-mii unloads once the list is left', ok)
        capture('after-back')
        cfl_lines = [l for l in log_text().splitlines() if 'cfl: ' in l]
        if args.no_mii:
            check('without a database cfl-mii says so in the log (and nothing on the screen)',
                  any('CFL_DB.dat' in l for l in cfl_lines), str(cfl_lines[:3]))
        else:
            others = [k for k in listed if k == 'mii']
            if real_db:
                ids = test_miis.recent_ids(db_path.read_bytes()) if db_path.exists() else None
                drawn = {bytes.fromhex(h)[0x0C:0x16] for n, h in test_miis.hosts() if n in names and kinds.get(n) == 'mii'}
                check('the recently-seen list holds the other consoles\' Miis drawn (the console\'s own database)',
                      ids is not None and drawn <= ids,
                      'not valid' if ids is None else f'{len(drawn & ids)} of {len(drawn)} drawn are listed, {len(ids)} listed')
            else:
                count = test_miis.recent_count(db_path.read_bytes()) if db_path.exists() else -2
                check('the recently-seen list holds the other consoles\' Miis drawn (not this console\'s own)',
                      count == len(others), f'{count} in the list, {len(others)} other Miis drawn')
            check('cfl-mii printed nothing (it found the database and wrote to it)', not cfl_lines, str(cfl_lines[:3]))
    finally:
        if proc is not None:
            emu.stop(proc)
        for p in procs:
            p.kill()
        time.sleep(1)
        if (sd / 'game.log').exists():
            shutil.copy2(sd / 'game.log', out / 'game.log')
        if emu.log_path(home).exists():
            shutil.copy2(emu.log_path(home), out / 'azahar_log.txt')
        print(f'{sum(1 for _, ok in results if ok)}/{len(results)} checks passed; {time.monotonic() - t0:.0f}s ->', out)
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


if __name__ == '__main__':
    main()
