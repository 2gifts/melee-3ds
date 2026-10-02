"""Minimal Slippi .slp reader for the determinism harness.

parse(path) -> Replay with:
  version (tuple), game_info (312-byte block), seed, gecko (bytes),
  frame_seed[frame], pre[(frame, port, follower)], post[(frame, port, follower)]
Online replays repeat frames that were rolled back; the last copy wins.
Offsets follow slippi-wiki SPEC.md (payload offsets exclude the command byte).
"""
import struct
from dataclasses import dataclass, field


def _events(data):
    i = data.find(b'raw[$U#l')
    length = struct.unpack('>I', data[i + 8:i + 12])[0]
    raw = data[i + 12:i + 12 + length] if length else data[i + 12:]
    assert raw[0] == 0x35, 'missing event payload sizes'
    n = raw[1]
    sizes = {}
    for k in range((n - 1) // 3):
        sizes[raw[2 + 3 * k]] = struct.unpack('>H', raw[3 + 3 * k:5 + 3 * k])[0]
    pos = 1 + n
    while pos < len(raw):
        cmd = raw[pos]
        size = sizes.get(cmd)
        if size is None or pos + 1 + size > len(raw):
            break
        yield cmd, raw[pos + 1:pos + 1 + size]
        pos += 1 + size


def _f(p, o):
    return struct.unpack('>f', p[o:o + 4])[0]


def _fbits(p, o):
    return struct.unpack('>I', p[o:o + 4])[0]


@dataclass
class Replay:
    version: tuple = ()
    game_info: bytes = b''
    seed: int = 0
    gecko: bytes = b''
    frame_seed: dict = field(default_factory=dict)
    pre: dict = field(default_factory=dict)
    post: dict = field(default_factory=dict)
    first: int = 0
    last: int = 0
    finalized: int = None


def parse(path):
    r = Replay()
    gecko = bytearray()
    for cmd, p in _events(open(path, 'rb').read()):
        if cmd == 0x36:
            r.version = tuple(p[0:3])
            r.game_info = bytes(p[4:4 + 312])
            r.seed = struct.unpack('>I', p[0x13C:0x140])[0]
        elif cmd == 0x10 and len(p) >= 515 and p[514] == 0x3D:
            size = struct.unpack('>H', p[512:514])[0]
            gecko += p[:size]
        elif cmd == 0x3A:
            frame, seed = struct.unpack('>iI', p[0:8])
            r.frame_seed[frame] = seed
        elif cmd == 0x37:
            frame = struct.unpack('>i', p[0:4])[0]
            port, follower = p[4], p[5]
            r.pre[(frame, port, follower)] = dict(
                seed=struct.unpack('>I', p[6:10])[0],
                action=struct.unpack('>H', p[10:12])[0],
                x=_fbits(p, 0xC), y=_fbits(p, 0x10), facing=_fbits(p, 0x14),
                lstick=(p[0x18:0x1C], p[0x1C:0x20]), cstick=(p[0x20:0x24], p[0x24:0x28]),
                trigger=p[0x28:0x2C], buttons=p[0x2C:0x30], phys=p[0x30:0x32],
                raw=(p[0x3A] if len(p) > 0x3A else 0,
                     p[0x3F] if len(p) > 0x3F else 0,
                     p[0x40] if len(p) > 0x40 else 0,
                     p[0x41] if len(p) > 0x41 else 0),
                percent=_fbits(p, 0x3B) if len(p) > 0x3E else None,
            )
        elif cmd == 0x38:
            frame = struct.unpack('>i', p[0:4])[0]
            port, follower = p[4], p[5]
            r.post[(frame, port, follower)] = dict(
                char=p[6], action=struct.unpack('>H', p[7:9])[0],
                x=_fbits(p, 9), y=_fbits(p, 0xD), facing=_fbits(p, 0x11),
                percent=_fbits(p, 0x15), shield=_fbits(p, 0x19),
                stocks=p[0x20], anim=_fbits(p, 0x21),
            )
        elif cmd == 0x3C:
            frame, latest = struct.unpack('>ii', p[0:8])
            r.finalized = latest
    r.gecko = bytes(gecko)
    frames = [k[0] for k in r.pre]
    if frames:
        r.first, r.last = min(frames), max(frames)
    return r


def f32(bits):
    return struct.unpack('>f', struct.pack('>I', bits))[0]
