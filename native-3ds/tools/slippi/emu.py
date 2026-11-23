"""Private Azahar instances for the Slippi experiment.

Like tools/qa_sweep/instances.py, but the instances live in this checkout's
build/azahar/<name> (never in the shared .toolchain), so several worktrees of
the experiment can run emulators side by side.

Windows: program files are hard links to the workspace emulator; the game
files folder is a junction to its SD copy.
Linux (the Docker test runner, docker/emu-test.sh): each instance gets its own
XDG data/config directories under build/azahar/<name> and runs the system
`azahar` ($AZAHAR) headless on Qt's offscreen platform; the game files are
symlinked from $MELEE_GAME_FILES and $MELEE_SLIPPI_FILES.

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
WINDOWS = sys.platform == 'win32'
MASTER = ROOT / '.toolchain/azahar/azahar-windows-msys2-2126.1'
BASE = Path(os.environ.get('MELEE_EMU_DIR') or ROOT / 'build/azahar')
GAME_FILES = Path(os.environ.get('MELEE_GAME_FILES') or ROOT / 'dist/native-alpha/3ds/melee/files')
SLIPPI_FILES = Path(os.environ.get('MELEE_SLIPPI_FILES') or ROOT / 'dist/native-alpha/3ds/melee/slippi/files')


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


def user_dir(home):
    """Azahar's user directory (sdmc, nand, log) for an instance."""
    return home / 'user' if WINDOWS else home / 'data/azahar-emu'


def log_path(home):
    return user_dir(home) / 'log/azahar_log.txt'


def _symlink(link, target):
    if link.is_symlink() or link.exists():
        return
    link.parent.mkdir(parents=True, exist_ok=True)
    link.symlink_to(Path(target).resolve(), target_is_directory=True)


GRAPHICS_API = {'software': 0, 'opengl': 1, 'vulkan': 2}


def _prepare_linux(name, gdb_port, graphics):
    home = BASE / name
    user = user_dir(home)
    (user / 'log').mkdir(parents=True, exist_ok=True)
    melee = user / 'sdmc/3ds/melee'
    melee.mkdir(parents=True, exist_ok=True)
    _symlink(melee / 'files', GAME_FILES)
    (melee / 'slippi').mkdir(exist_ok=True)
    if SLIPPI_FILES.exists():
        _symlink(melee / 'slippi/files', SLIPPI_FILES)
    config = home / 'config/azahar-emu/qt-config.ini'
    config.parent.mkdir(parents=True, exist_ok=True)
    api = GRAPHICS_API[graphics or os.environ.get('MELEE_GRAPHICS', 'software')]

    def setting(key, value):
        return f'{key}={value}\n{key}\\default=false\n'
    # Only the keys that differ from Azahar's defaults. frame_limit 100 keeps
    # emulation at game speed; the tests never need it faster.
    config.write_text('[Debugging]\n' + setting('use_gdbstub', 'true') + setting('gdbstub_port', gdb_port) +
                      '\n[Renderer]\n' + setting('graphics_api', api) + setting('use_vsync_new', 'false') +
                      '\n[UI]\n' + setting('confirmClose', 'false') + setting('firstStart', 'false') +
                      setting('check_for_update_on_start', 'false'), encoding='utf-8')
    return home


def prepare(name, gdb_port=24800, graphics=None):
    """graphics: None keeps the master's backend, 'opengl' or 'vulkan' (Linux
    also 'software'; default $MELEE_GRAPHICS or software)."""
    if not WINDOWS:
        return _prepare_linux(name, gdb_port, graphics)
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
        elif graphics and line.startswith('graphics_api='):
            line = 'graphics_api=' + ('1' if graphics == 'opengl' else '2')
        elif graphics and line.startswith(r'graphics_api\default='):
            line = r'graphics_api\default=false'
        lines.append(line)
    config.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return home


def elf_symbols(path):
    """{name: address} from an ELF32 symbol table (little-endian ARM); pure
    Python so no host binutils or llvm-nm is needed."""
    import struct
    data = Path(path).read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise ValueError(f'{path}: not a little-endian ELF32 file')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2E)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for _name, kind, _flags, _addr, offset, size, link, _info, _align, entsize in sections:
        if kind != 2:  # SHT_SYMTAB
            continue
        str_off, str_size = sections[link][4], sections[link][5]
        strings = data[str_off:str_off + str_size]
        for i in range(size // entsize):
            name_off, value, _size, _info2, _other, shndx = struct.unpack_from('<IIIBBH', data, offset + i * entsize)
            if shndx and name_off:
                out[strings[name_off:strings.index(b'\0', name_off)].decode()] = value
    return out


def sd(home):
    return user_dir(home) / 'sdmc/3ds/melee'


def launch(home, dsx, hidden=True):
    if not WINDOWS:
        env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / 'data'), XDG_CONFIG_HOME=str(home / 'config'),
                   XDG_RUNTIME_DIR=str(_runtime_dir(home)))
        env.setdefault('QT_QPA_PLATFORM', 'offscreen')
        if os.environ.get('MELEE_QT_PLATFORM'):
            env['QT_QPA_PLATFORM'] = os.environ['MELEE_QT_PLATFORM']
        out = open(home / 'azahar.out', 'wb')
        return subprocess.Popen([os.environ.get('AZAHAR', 'azahar'), str(dsx)], env=env, stdout=out,
                                stderr=subprocess.STDOUT, start_new_session=True)
    info = subprocess.STARTUPINFO()
    if hidden:
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = 0
    return subprocess.Popen([str(home / 'azahar.exe'), str(dsx)], startupinfo=info)


def _runtime_dir(home):
    path = home / 'run'
    path.mkdir(exist_ok=True)
    path.chmod(0o700)
    return path


def stop(proc):
    if proc.poll() is None:
        if WINDOWS:
            proc.kill()
        else:
            import signal
            try:
                os.killpg(proc.pid, signal.SIGKILL)  # the emulator may have child processes
            except ProcessLookupError:
                pass
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
