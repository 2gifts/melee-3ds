"""Test Miis for PC hosts (group.ini test_mii=HEX): Azahar's built-in Mii ("Citra", what its FRD_GetMyMii returns, as
logged by the game's "group: Mii for the beacon" line) and variants of it with another name, sex, skin, hair and
favourite colour, each sealed with CFL's CRC-16 so browsers accept them. Field offsets: 3dbrew's Mii data format.

    python tools/group/test_miis.py            # NAME HEX lines
"""
import sys

AZAHAR = ('03001030d285b6b300c8850a98391ee440f407b737100000a60043006900740072006100000000000000000000004040000021010268'
          '441826344614811217680d0000290052485066006c0054006f006200690000000000000000000000056c')


def crc16(data):
    crc = 0
    for byte in data:
        for _ in range(8):
            crc = ((0x1021 ^ (crc << 1)) & 0xFFFF) if crc & 0x8000 else (crc << 1) & 0xFFFF
        crc ^= byte
    return crc


def seal(mii):
    mii = bytearray(mii[:94]) + b'\0\0'
    c = crc16(mii)
    mii[94], mii[95] = c >> 8, c & 0xFF
    return bytes(mii)


def variant(name, female, skin, hair_style, hair_color, favorite_color, base=AZAHAR):
    m = bytearray(bytes.fromhex(base))
    m[0x0F] ^= (sum(name.encode()) & 0x7F) | 1   # the Mii ID's low byte: another Mii than this console's own
    info = m[0x18] | m[0x19] << 8
    info = (info & ~1 & ~(0xF << 10)) | (1 if female else 0) | (favorite_color & 0xF) << 10
    m[0x18], m[0x19] = info & 0xFF, info >> 8
    m[0x1A:0x2E] = name[:10].encode('utf-16-le').ljust(20, b'\0')
    m[0x30] = (m[0x30] & 0x1F) | (skin & 7) << 5
    m[0x32] = hair_style
    m[0x33] = (m[0x33] & ~7) | (hair_color & 7)
    return seal(m)


HOSTS = [  # name, female, skin, hair style, hair colour, favourite colour
    ('Dana', 1, 1, 33, 3, 0),
    ('Eli', 0, 3, 12, 0, 2),
    ('Fumiko', 1, 0, 98, 6, 8),
    ('Gus', 0, 4, 45, 2, 5),
    ('Hana', 1, 2, 70, 7, 11),
    ('Ivo', 0, 5, 5, 1, 4),
]


DB_SIZE = 0xE4C0
RECENT_AT, RECENT_SIZE = 0xC820, 0x1CA0


def cfl_db(owner_hex=AZAHAR):
    """CFL_DB.dat (shared extdata F000000B) as cfl-mii reads and writes it: "CFOG", the owner Mii's slot (0), 100
    records of 92 bytes (MiiData; the owner's in slot 0, the rest empty), and the recently-seen section at 0xC820:
    "CFRA", a count (0), 100 history bytes and 100 records of 72 bytes, with a CRC-16 at its end."""
    db = bytearray(DB_SIZE)
    db[0:4] = b'CFOG'
    db[8:8 + 92] = bytes.fromhex(owner_hex)[:92]
    region = bytearray(RECENT_SIZE)
    region[0:4] = b'CFRA'
    c = crc16(region)
    region[-2], region[-1] = c >> 8, c & 0xFF
    db[RECENT_AT:RECENT_AT + RECENT_SIZE] = region
    return bytes(db)


def recent_count(db):
    """How many Miis the recently-seen section lists (0 if it is not valid)."""
    region = db[RECENT_AT:RECENT_AT + RECENT_SIZE]
    if len(region) != RECENT_SIZE or region[:4] != b'CFRA' or crc16(region) != 0:
        return -1
    return int.from_bytes(region[4:8], 'little')


def recent_ids(db):
    """The Mii IDs (10 bytes at 0x0C) of the Miis the recently-seen section lists, or None if it is not valid. A real
    console's list is often full (100), so a new Mii replaces the oldest and the count does not change."""
    region = db[RECENT_AT:RECENT_AT + RECENT_SIZE]
    if len(region) != RECENT_SIZE or region[:4] != b'CFRA' or crc16(region) != 0:
        return None
    count = min(int.from_bytes(region[4:8], 'little'), 100)
    ids = set()
    for slot in region[8:8 + count]:
        if slot < 100:
            record = region[0x6C + slot * 72:0x6C + slot * 72 + 72]
            ids.add(bytes(record[0x0C:0x16]))
    return ids


def hosts():
    return [(n, variant(n, *rest).hex()) for n, *rest in HOSTS]


def check():
    base = bytes.fromhex(AZAHAR)
    assert len(base) == 96 and crc16(base) == 0, 'the Azahar Mii must carry a valid CRC'
    for _, h in hosts():
        assert crc16(bytes.fromhex(h)) == 0 and bytes.fromhex(h)[0x0C:0x16] != base[0x0C:0x16]
    assert recent_count(cfl_db()) == 0


if __name__ == '__main__':
    check()
    for n, h in hosts():
        print(n, h)
    sys.exit(0)
