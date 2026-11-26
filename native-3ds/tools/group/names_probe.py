"""The names under the group results screen's windows, read from an emulated console's memory.

group_results.py's patch keeps, for tests: mp_res_label[4][24] (the game text drawn under each window, b'' = vanilla's
fighter name) and mp_res_named (bit n: window n's fighter-name graphic is hidden). `sis` is what the game shows for a
console's name (port/3ds/slippi/slippi_ui.c sis() and port/3ds/name_text.c: ASCII letters, digits and a few signs as
they are, other ASCII signs full width, a character the font has (kana, some kanji) as its Shift-JIS code, an accented
letter as its base letter, anything else a full-width '?'; the table is port/3ds/name_glyphs.h)."""
import re
from pathlib import Path

_HEADER = Path(__file__).resolve().parents[2] / 'port/3ds/name_glyphs.h'
_TEXT = _HEADER.read_text(encoding='utf-8')
_GLYPHS = {int(cp, 16): int(sj, 16) for cp, sj in re.findall(r'\{0x([0-9A-F]+), 0x([0-9A-F]+), \d+\}', _TEXT)}
_FOLD = {int(cp, 16): bytes(int(b, 16) for b in re.findall(r'\\x([0-9A-F]{2})', to)).decode() + re.sub(r'\\x[0-9A-F]{2}', '', to)
         for cp, to in re.findall(r'\{0x([0-9A-F]+), "([^"]*)"\}', _TEXT)}

_WIDE = {'!': 0x8149, '#': 0x8194, '$': 0x8190, '%': 0x8193, '&': 0x8195, '(': 0x8169, ')': 0x816a, '*': 0x8196,
         '+': 0x817b, '/': 0x815e, ';': 0x8147, '<': 0x8183, '=': 0x8181, '>': 0x8184, '?': 0x8148, '@': 0x8197,
         '[': 0x816d, '\\': 0x815f, ']': 0x816e, '^': 0x814f, '_': 0x8151, '`': 0x814d, '{': 0x816f, '|': 0x8162,
         '}': 0x8170, '~': 0x8160}


def wire_name(name, limit=10):
    """A name as group.ini name= keeps it (name_clean): at most `limit` characters."""
    return name[:limit]


def sis(name):
    out = bytearray()
    for ch in name:
        if ch.isascii() and (ch.isalnum() or ch in ' .,-:\'"'):
            out.append(ord(ch))
        elif not ch.isascii():
            if ord(ch) in _GLYPHS:
                out += _GLYPHS[ord(ch)].to_bytes(2, 'big')
            elif ord(ch) in _FOLD:
                out += sis(_FOLD[ord(ch)])
            else:
                out += (0x8148).to_bytes(2, 'big')
        elif ch in _WIDE:
            out += _WIDE[ch].to_bytes(2, 'big')
    return bytes(out)


def labels(stub):
    """(the four windows' labels as bytes, the named mask)."""
    raw = stub.peek('mp_res_label', 96)
    return [raw[24 * i:24 * i + 24].split(b'\0')[0] for i in range(4)], stub.peek('mp_res_named', 1)[0]
