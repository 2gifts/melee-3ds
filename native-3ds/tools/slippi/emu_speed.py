"""How fast does the development build run in Azahar here? Samples the engine's frame counter over the GDB stub
(game speed is 60 frames per second). Headless, as the other emulator tests; compare with the web setup
(docker/emu-web.sh with MELEE_WEB_GDB=1).

    docker/emu-test.sh run tools/slippi/emu_speed.py [--samples 6] [--every 15]"""
import argparse
import os
import socket
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import emu  # noqa: E402
from gdb_probe import packet, receive  # noqa: E402

PORT = 24863


def frames(addr):
    with socket.create_connection(('127.0.0.1', PORT), 5) as s:
        s.settimeout(10)
        packet(s, '?')
        receive(s)
        packet(s, f'm{addr:x},4')
        v = struct.unpack('<I', bytes.fromhex(receive(s)))[0]
        packet(s, 'c')
        return v


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--samples', type=int, default=6)
    ap.add_argument('--every', type=float, default=15)
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--elf', default=os.environ.get('MELEE_ELF') or str(emu.ROOT / 'build/game-opt/melee.elf'))
    args = ap.parse_args()
    addr = emu.elf_symbols(args.elf)['engine_frames']
    home = emu.prepare('speed', PORT)
    sd = emu.sd(home)
    (sd / 'slippi').mkdir(parents=True, exist_ok=True)
    (sd / 'slippi' / 'config.ini').write_text('local_group=1\nlocal_udp=1\nlocal_port=44100\nprefetch=0\n')
    proc = emu.launch(home, Path(args.dsx).resolve(), hidden=True)
    try:
        time.sleep(60)
        last = None
        for _ in range(args.samples):
            try:
                t, f = time.time(), frames(addr)
            except (OSError, TimeoutError) as e:
                print('stub:', e, flush=True)
                time.sleep(args.every)
                continue
            rate = '' if last is None else f'  {(f - last[1]) / (t - last[0]):.1f} frames/s'
            print(f'engine_frames={f}{rate}', flush=True)
            last = (t, f)
            time.sleep(args.every)
    finally:
        emu.stop(proc)


if __name__ == '__main__':
    main()
