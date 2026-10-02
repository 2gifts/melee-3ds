"""Private Azahar instances for the Slippi experiment.

Like tools/qa_sweep/instances.py, but the instances live in this checkout's
build/azahar/<name> (never in the shared .toolchain), so several worktrees of
the experiment can run emulators side by side. Program files are hard links to
the workspace emulator; the game files folder is a junction to its SD copy.

    python tools/slippi/emu.py prepare NAME [--gdb-port N]
    python tools/slippi/emu.py run NAME DSX [--seconds S]   # launch, wait, stop

Library use: prepare(name, port) -> home; launch(home, dsx) -> Popen.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / '.toolchain/azahar/azahar-windows-msys2-2126.1'
BASE = ROOT / 'build/azahar'


def _link_tree(source, target):
    target.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        dest = target / item.name
        if item.is_dir():
            _link_tree(item, dest)
        elif not dest.exists():
            try:
                os.link(item, dest)
            except OSError:
                shutil.copy2(item, dest)


def _junction(link, target):
    if link.exists():
        return
    subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(target)], check=True, capture_output=True)


def prepare(name, gdb_port=24800):
    home = BASE / name
    if not (home / 'azahar.exe').exists():
        home.mkdir(parents=True, exist_ok=True)
        for item in MASTER.iterdir():
            if item.name == 'user':
                continue
            if item.is_dir():
                _link_tree(item, home / item.name)
            elif not (home / item.name).exists():
                os.link(item, home / item.name)
        user = home / 'user'
        for sub in ('config', 'nand', 'sysdata'):
            if (MASTER / 'user' / sub).exists():
                shutil.copytree(MASTER / 'user' / sub, user / sub, dirs_exist_ok=True)
        (user / 'log').mkdir(parents=True, exist_ok=True)
        melee = user / 'sdmc/3ds/melee'
        melee.mkdir(parents=True, exist_ok=True)
        _junction(melee / 'files', MASTER / 'user/sdmc/3ds/melee/files')
    config = home / 'user/config/qt-config.ini'
    lines = []
    for line in config.read_text(encoding='utf-8').splitlines():
        if line.startswith('gdbstub_port='):
            line = f'gdbstub_port={gdb_port}'
        elif line.startswith(r'gdbstub_port\default='):
            line = r'gdbstub_port\default=false'
        elif line.startswith('use_gdbstub='):
            line = 'use_gdbstub=true'
        lines.append(line)
    config.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return home


def sd(home):
    return home / 'user/sdmc/3ds/melee'


def launch(home, dsx, hidden=True):
    info = subprocess.STARTUPINFO()
    if hidden:
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = 0
    return subprocess.Popen([str(home / 'azahar.exe'), str(dsx)], startupinfo=info)


def stop(proc):
    if proc.poll() is None:
        proc.kill()
        proc.wait(30)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('name')
    p.add_argument('--gdb-port', type=int, default=24800)
    r = sub.add_parser('run')
    r.add_argument('name')
    r.add_argument('dsx')
    r.add_argument('--seconds', type=float, default=60)
    r.add_argument('--gdb-port', type=int, default=24800)
    r.add_argument('--show', action='store_true')
    args = ap.parse_args()
    home = prepare(args.name, args.gdb_port)
    if args.cmd == 'prepare':
        print(home)
        return
    proc = launch(home, Path(args.dsx).resolve(), hidden=not args.show)
    try:
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline and proc.poll() is None:
            time.sleep(0.5)
    finally:
        stop(proc)
    log = sd(home) / 'game.log'
    if log.exists():
        sys.stdout.write(log.read_text(errors='replace')[-4000:])


if __name__ == '__main__':
    main()
