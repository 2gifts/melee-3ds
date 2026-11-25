"""The names under the group results screen's windows, read from an emulated console's memory.

group_results.py's patch keeps, for tests: mp_res_label[4][24] (the game text drawn under each window, b'' = vanilla's
fighter name) and mp_res_named (bit n: window n's fighter-name graphic is hidden). `sis` is what the game shows for a
console's name (port/3ds/slippi/slippi_ui.c sis(): ASCII letters, digits and a few signs as they are, other ASCII signs
full width, every non-ASCII character a full-width '?')."""

_WIDE = {'!': 0x8149, '#': 0x8194, '$': 0x8190, '%': 0x8193, '&': 0x8195, '(': 0x8169, ')': 0x816a, '*': 0x8196,
         '+': 0x817b, '/': 0x815e, ';': 0x8147, '<': 0x8183, '=': 0x8181, '>': 0x8184, '?': 0x8148, '@': 0x8197,
         '[': 0x816d, '\\': 0x815f, ']': 0x816e, '^': 0x814f, '_': 0x8151, '`': 0x814d, '{': 0x816f, '|': 0x8162,
         '}': 0x8170, '~': 0x8160}


def wire_name(name, limit=11):
    """A name as group.ini name= keeps it: at most `limit` UTF-8 bytes, never cut inside a character."""
    out = b''
    for ch in name:
        b = ch.encode()
        if len(out) + len(b) > limit:
            break
        out += b
    return out.decode()


def sis(name):
    out = bytearray()
    for ch in name:
        if ch.isascii() and (ch.isalnum() or ch in ' .,-:\'"'):
            out.append(ord(ch))
        elif not ch.isascii():
            out += (0x8148).to_bytes(2, 'big')
        elif ch in _WIDE:
            out += _WIDE[ch].to_bytes(2, 'big')
    return bytes(out)


def labels(stub):
    """(the four windows' labels as bytes, the named mask)."""
    raw = stub.peek('mp_res_label', 96)
    return [raw[24 * i:24 * i + 24].split(b'\0')[0] for i in range(4)], stub.peek('mp_res_named', 1)[0]
