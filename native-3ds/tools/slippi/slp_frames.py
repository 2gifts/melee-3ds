"""Minimal .slp reader: per-frame pre-frame inputs and post-frame state.

Usage: python slp_frames.py REPLAY.slp [--dump N]
"""
import struct
import sys


def read_raw(path):
    d = open(path, "rb").read()
    i = d.find(b"raw[$U#l")
    length = struct.unpack(">I", d[i + 8:i + 12])[0]
    raw = d[i + 12:i + 12 + length]
    assert raw[0] == 0x35
    n = raw[1]
    sizes = {}
    for k in range((n - 1) // 3):
        c = raw[2 + 3 * k]
        sizes[c] = struct.unpack(">H", raw[3 + 3 * k:5 + 3 * k])[0]
    pos = 1 + n
    events = []
    while pos < len(raw):
        c = raw[pos]
        if c not in sizes:
            break
        events.append((c, raw[pos + 1:pos + 1 + sizes[c]]))
        pos += 1 + sizes[c]
    return events


def f32(b, o):
    return struct.unpack(">f", b[o - 1:o + 3])[0]


def u32(b, o):
    return struct.unpack(">I", b[o - 1:o + 3])[0]


def u16(b, o):
    return struct.unpack(">H", b[o - 1:o + 1])[0]


def parse(path):
    """Returns (game_start_payload, frames) where frames[frame][port] = dict."""
    frames = {}
    start = None
    for c, p in read_raw(path):
        if c == 0x36:
            start = p
        elif c in (0x37, 0x38):
            frame = struct.unpack(">i", p[0:4])[0]
            port, follower = p[4], p[5]
            if follower:
                continue
            rec = frames.setdefault(frame, {}).setdefault(port, {})
            if c == 0x37:
                rec.update(
                    seed=u32(p, 0x7), pre_as=u16(p, 0xB),
                    stick=(f32(p, 0x19), f32(p, 0x1D)),
                    cstick=(f32(p, 0x21), f32(p, 0x25)),
                    trigger=f32(p, 0x29), buttons=u32(p, 0x2D),
                    phys_buttons=u16(p, 0x31),
                    phys_lr=(f32(p, 0x33), f32(p, 0x37)),
                    raw_x=struct.unpack(">b", p[0x3A:0x3B])[0],
                )
            else:
                rec.update(
                    char=p[6], action=u16(p, 0x8), x=f32(p, 0xA), y=f32(p, 0xE),
                    facing=f32(p, 0x12), percent=f32(p, 0x16), shield=f32(p, 0x1A),
                    stocks=p[0x20], as_frame=f32(p, 0x22),
                )
    return start, frames


if __name__ == "__main__":
    start, frames = parse(sys.argv[1])
    keys = sorted(frames)
    print("frames", keys[0], "..", keys[-1], "count", len(keys))
    n = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[2] == "--dump" else 5
    for fr in keys[:3] + keys[200:200 + n] + keys[-2:]:
        for port, r in sorted(frames[fr].items()):
            print(fr, port, {k: (round(v, 4) if isinstance(v, float) else v) for k, v in r.items()})
