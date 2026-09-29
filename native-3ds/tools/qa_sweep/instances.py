"""Parallel Azahar instances for the QA sweep.

Each instance is a portable copy of the workspace emulator: program files
are hard links, the config is copied with its own GDB port, and the SD card
is a private folder whose read-only game files are a junction to the master
copy. Instances therefore keep separate save files and logs.
"""
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / '.toolchain/azahar/azahar-windows-msys2-2126.1'
BASE = ROOT / '.toolchain/azahar-sweep'
BASE_PORT = 24700


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


def prepare(index):
    """Create (once) and return (directory, gdb_port) for instance INDEX."""
    home = BASE / f'i{index}'
    port = BASE_PORT + index
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
        for name in ('config', 'nand', 'sysdata'):
            if (MASTER / 'user' / name).exists():
                shutil.copytree(MASTER / 'user' / name, user / name, dirs_exist_ok=True)
        (user / 'log').mkdir(parents=True, exist_ok=True)
        melee = user / 'sdmc/3ds/melee'
        melee.mkdir(parents=True, exist_ok=True)
        _junction(melee / 'files', MASTER / 'user/sdmc/3ds/melee/files')
        if (MASTER / 'user/sdmc/3ds/melee/visuals').exists():
            _junction(melee / 'visuals', MASTER / 'user/sdmc/3ds/melee/visuals')
    config = home / 'user/config/qt-config.ini'
    text = config.read_text(encoding='utf-8')
    lines = []
    for line in text.splitlines():
        if line.startswith('gdbstub_port='):
            line = f'gdbstub_port={port}'
        elif line.startswith(r'gdbstub_port\default='):
            line = r'gdbstub_port\default=false'
        elif line.startswith('use_gdbstub='):
            line = 'use_gdbstub=true'
        elif line.startswith('graphics_api='):
            # MP_SWEEP_GRAPHICS=opengl cross-checks Vulkan-backend faults.
            line = 'graphics_api=' + ('1' if os.environ.get('MP_SWEEP_GRAPHICS') == 'opengl' else '2')
        elif line.startswith(r'graphics_api\default='):
            line = r'graphics_api\default=false'
        lines.append(line)
    config.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return home, port


def reset_saves(home):
    """Remove an instance's memory-card saves (both profiles)."""
    shutil.rmtree(home / 'user/sdmc/3ds/melee/saves', ignore_errors=True)
