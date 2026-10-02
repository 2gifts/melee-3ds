"""Debug helper: play a replay until its output stops growing, then read
engine state over GDB.

    python tools/slippi/replay_stall.py REPLAY.slp SYMBOL[:size] ...
"""
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402
from replay_pack import pack  # noqa: E402
from slp import parse  # noqa: E402

ROOT = emu.ROOT
PORT = 24821


def symbols(elf):
    nm = ROOT / '.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'
    out = subprocess.check_output([str(nm), str(elf)], text=True)
    return {p[2]: int(p[0], 16) for line in out.splitlines() if len(p := line.split()) == 3}


def main():
    slp = sys.argv[1]
    wanted = sys.argv[2:]
    syms = symbols(ROOT / 'build/game-opt/melee.elf')
    home = emu.prepare('stall', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi/replay.bin').write_bytes(pack(parse(slp)))
    out = sd / 'slippi/replay-out.bin'
    out.unlink(missing_ok=True)
    proc = emu.launch(home, (ROOT / 'dist/3ds/melee/melee-development.3dsx').resolve())
    try:
        size, still = -1, 0
        while still < 45 and proc.poll() is None:
            time.sleep(1)
            now = out.stat().st_size if out.exists() else 0
            still = still + 1 if now == size and now > 0 else 0
            size = now
        print('output stopped at', size // 40, 'records')
        with socket.create_connection(('127.0.0.1', PORT), 10) as s:
            s.settimeout(20)
            packet(s, '?')
            receive(s)
            for spec in wanted:
                name, _, n = spec.partition(':')
                n = int(n or 4)
                matches = [k for k in syms if k == name or k.startswith(name + '.')]
                if not matches:
                    print(name, 'not found')
                    continue
                packet(s, f'm{syms[matches[0]]:x},{n:x}')
                print(name, receive(s))
            packet(s, 'c')
    finally:
        emu.stop(proc)
        (sd / 'slippi/replay.bin').unlink(missing_ok=True)


if __name__ == '__main__':
    main()
