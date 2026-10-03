"""Minimal VCDIFF (RFC 3284) decoder for Slippi's game-file patches.

Slippi Dolphin ships its menu art changes (MnMaAll, SdMenu, MnSlMap, SdSlChr,
MnExtAll) as VCDIFF deltas against the player's own disc files
(Data/Sys/GameFiles/GALE01/*.diff). This applies one: default code table,
no secondary compressor (what those files use); the open-vcdiff Adler-32
window checksum extension is read and verified.

    python tools/slippi/vcdiff.py SOURCE DELTA OUT
"""
import sys
import zlib
from pathlib import Path

VCD_SOURCE, VCD_TARGET, VCD_ADLER32 = 0x01, 0x02, 0x04
NOOP, ADD, RUN, COPY = 0, 1, 2, 3


def default_code_table():
    table = [(RUN, 0, 0, NOOP, 0, 0)]
    table += [(ADD, s, 0, NOOP, 0, 0) for s in range(0, 18)]
    for mode in range(9):
        table.append((COPY, 0, mode, NOOP, 0, 0))
        table += [(COPY, s, mode, NOOP, 0, 0) for s in range(4, 19)]
    for mode in range(6):
        for add in range(1, 5):
            table += [(ADD, add, 0, COPY, s, mode) for s in range(4, 7)]
    for mode in range(6, 9):
        table += [(ADD, add, 0, COPY, 4, mode) for add in range(1, 5)]
    table += [(COPY, 4, mode, ADD, 1, 0) for mode in range(9)]
    assert len(table) == 256
    return table


CODE_TABLE = default_code_table()


class Reader:
    def __init__(self, data, pos=0, end=None):
        self.data, self.pos = data, pos
        self.end = len(data) if end is None else end

    def byte(self):
        if self.pos >= self.end:
            raise ValueError('truncated delta')
        b = self.data[self.pos]
        self.pos += 1
        return b

    def varint(self):
        v = 0
        while True:
            b = self.byte()
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                return v

    def take(self, n):
        if self.pos + n > self.end:
            raise ValueError('truncated delta')
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out


def decode(source, delta):
    r = Reader(delta)
    if r.take(4) != b'\xd6\xc3\xc4\x00':
        raise ValueError('not a VCDIFF file')
    hdr = r.byte()
    if hdr & 0x01:
        raise ValueError('secondary compression is not supported')
    if hdr & 0x02:
        raise ValueError('custom code tables are not supported')
    if hdr & 0x04:
        r.take(r.varint())   # application header
    out = bytearray()
    while r.pos < len(delta):
        win = r.byte()
        seg = b''
        if win & (VCD_SOURCE | VCD_TARGET):
            seg_len, seg_pos = r.varint(), r.varint()
            seg = bytes(source[seg_pos:seg_pos + seg_len]) if win & VCD_SOURCE else bytes(out[seg_pos:seg_pos + seg_len])
        r.varint()                              # delta encoding length
        target_len = r.varint()
        if r.byte():
            raise ValueError('compressed sections are not supported')
        data_len, inst_len, addr_len = r.varint(), r.varint(), r.varint()
        checksum = None
        if win & VCD_ADLER32:
            checksum = int.from_bytes(r.take(4), 'big')
        data = Reader(r.take(data_len))
        inst = Reader(r.take(inst_len))
        addr = Reader(r.take(addr_len))
        near, same, near_slot = [0] * 4, [0] * (3 * 256), 0
        target = bytearray()
        slen = len(seg)

        def address(here, mode):
            nonlocal near_slot
            if mode == 0:
                a = addr.varint()
            elif mode == 1:
                a = here - addr.varint()
            elif mode < 6:
                a = near[mode - 2] + addr.varint()
            else:
                a = same[(mode - 6) * 256 + addr.byte()]
            near[near_slot] = a
            near_slot = (near_slot + 1) % 4
            same[a % (3 * 256)] = a
            return a

        while inst.pos < inst.end:
            code = CODE_TABLE[inst.byte()]
            for kind, size, mode in ((code[0], code[1], code[2]), (code[3], code[4], code[5])):
                if kind == NOOP:
                    continue
                if size == 0:
                    size = inst.varint()
                if kind == ADD:
                    target += data.take(size)
                elif kind == RUN:
                    target += bytes([data.byte()]) * size
                else:
                    a = address(slen + len(target), mode)
                    for _ in range(size):
                        target.append(seg[a] if a < slen else target[a - slen])
                        a += 1
        if len(target) != target_len:
            raise ValueError(f'window size {len(target)} != {target_len}')
        if checksum is not None and (zlib.adler32(bytes(target)) & 0xFFFFFFFF) != checksum:
            raise ValueError('window checksum mismatch (a different disc file?)')
        out += target
    return bytes(out)


def main():
    source, delta, out = (Path(a) for a in sys.argv[1:4])
    result = decode(source.read_bytes(), delta.read_bytes())
    out.write_bytes(result)
    print(f'{out}: {len(result)} bytes')


if __name__ == '__main__':
    main()
