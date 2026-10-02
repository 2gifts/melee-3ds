"""Pack a Slippi .slp into replay.bin for port/engine/slippi/replay.c.

Layout (big-endian):
  0x000 'SLR1'  0x004 u32 slp version  0x008 game info block (0x138 bytes)
  0x140 u32 seed  0x144 s32 first frame  0x148 u32 frame count
  0x14C frames: u32 frame-start seed, u8 entries, 3 pad, then per entry 32 bytes:
        u8 port, u8 follower, s8 raw stick x/y, s8 raw c-stick x/y, 2 pad,
        f32 stick x/y, c-stick x/y, trigger (bits as recorded), u32 buttons

    python tools/slippi/replay_pack.py REPLAY.slp OUT.bin
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from slp import parse  # noqa: E402


def pack(replay):
    r = replay
    out = bytearray(b'SLR1')
    out += struct.pack('>I', (r.version[0] << 16) | (r.version[1] << 8) | r.version[2])
    out += r.game_info
    assert len(out) == 0x140
    frames = list(range(r.first, r.last + 1))
    out += struct.pack('>IiI', r.seed, r.first, len(frames))
    by_frame = {}
    for (frame, port, follower), pre in r.pre.items():
        by_frame.setdefault(frame, []).append((port, follower, pre))
    for frame in frames:
        entries = sorted(by_frame.get(frame, []), key=lambda e: (e[0], e[1]))
        out += struct.pack('>IB3x', r.frame_seed.get(frame, 0), len(entries))
        for port, follower, pre in entries:
            out += struct.pack('>BB4B2x', port, follower, *pre['raw'])
            out += pre['lstick'][0] + pre['lstick'][1] + pre['cstick'][0] + pre['cstick'][1]
            out += pre['trigger'] + pre['buttons']
    return bytes(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    r = parse(src)
    data = pack(r)
    Path(dst).write_bytes(data)
    print(f'{src}: version {".".join(map(str, r.version))}, frames {r.first}..{r.last}, '
          f'{len(data)} bytes, stage {struct.unpack(">H", r.game_info[0xE:0x10])[0]}')


if __name__ == '__main__':
    main()
