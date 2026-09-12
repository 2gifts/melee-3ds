"""Read back a local CIA and compare its executable with the finalized BE8 ELF."""
import argparse
import hashlib
import json
import struct
import wave
from pathlib import Path

from be8_image import ElfImage
from home_banner_release import verify_release_model, verify_release_container

TITLE_ID = 0x000400000F4D4500


def u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


def align(n, size=64):
    return (n+size-1) & -size


def blz(data):
    extra = u32(data, len(data)-4)
    if not extra:
        return data[:-4]
    info = u32(data, len(data)-8)
    assert 8 <= info >> 24 <= 11
    end = len(data)-(info & 0xffffff)
    src = len(data)-(info >> 24)
    out = bytearray(data)+bytearray(extra)
    dst = len(out)
    while src > end:
        src -= 1
        flags = out[src]
        for bit in range(7, -1, -1):
            if src <= end:
                break
            if flags & (1 << bit):
                src -= 2
                token = out[src] | out[src+1] << 8
                length, distance = (token >> 12)+3, (token & 4095)+3
                for _ in range(length):
                    dst -= 1
                    assert end <= dst < dst+distance < len(out)
                    out[dst] = out[dst+distance]
            else:
                src -= 1
                dst -= 1
                out[dst] = out[src]
    assert dst == end
    return bytes(out)


def lz11(data, *, return_consumed=False):
    assert data[0] == 0x11
    length = int.from_bytes(data[1:4], 'little')
    assert 0 < length <= 0x80000
    out = bytearray()
    src = 4
    while len(out) < length:
        flags = data[src]; src += 1
        for bit in range(7, -1, -1):
            if len(out) == length:
                break
            a = data[src]; src += 1
            if not flags & (1 << bit):
                out.append(a)
                continue
            high = a >> 4
            if high < 2:
                b, c = data[src:src+2]; src += 2
                if high == 0:
                    count = ((a & 15) << 4 | b >> 4)+0x11
                    distance = ((b & 15) << 8 | c)+1
                else:
                    d = data[src]; src += 1
                    count = ((a & 15) << 12 | b << 4 | c >> 4)+0x111
                    distance = ((c & 15) << 8 | d)+1
            else:
                b = data[src]; src += 1
                count, distance = high+1, ((a & 15) << 8 | b)+1
            assert 0 < distance <= len(out) and len(out)+count <= length
            for _ in range(count):
                out.append(out[-distance])
    return (bytes(out),src) if return_consumed else bytes(out)


def verify_sound(data, wav_path):
    """Check CWAV offsets and compare both planar PCM channels to the WAV."""
    assert data[:4] == b'CWAV'
    assert struct.unpack_from('<HH', data, 4) == (0xfeff, 0x40)
    assert u32(data,8) == 0x02010000 and u32(data,12) == len(data)
    assert struct.unpack_from('<HH', data,16) == (2,0)
    blocks=[]
    for off,kind,magic in ((20,0x7000,b'INFO'),(32,0x7001,b'DATA')):
        ref,padding,start,size=struct.unpack_from('<HHII',data,off)
        assert ref == kind and padding == 0 and start % 32 == 0
        assert 0x40 <= start < start+size <= len(data)
        assert data[start:start+4] == magic and u32(data,start+4) == size
        blocks.append((start,size))
    (info,info_size),(samples,samples_size)=blocks
    assert info+info_size == samples and samples+samples_size == len(data)
    assert data[info+8:info+12] == bytes((1,0,0,0)), 'Expected nonlooping PCM16'
    rate,loop_start,frames,reserved,channels=struct.unpack_from('<5I',data,info+12)
    assert loop_start == reserved == 0 and channels == 2 and 0 < frames <= rate*3
    with wave.open(str(wav_path),'rb') as wav:
        assert (wav.getframerate(),wav.getnframes(),wav.getnchannels(),wav.getsampwidth()) == (rate,frames,channels,2)
        pcm=wav.readframes(frames)
    end=samples+32
    for channel in range(channels):
        kind,pad,rel=struct.unpack_from('<HHI',data,info+32+channel*8)
        entry=info+28+rel
        assert kind == 0x7100 and pad == 0 and info+48 <= entry <= info+info_size-20
        kind,pad,rel=struct.unpack_from('<HHI',data,entry)
        assert kind == 0x1f00 and pad == 0
        assert struct.unpack_from('<HHII',data,entry+8) == (0,0,0xffffffff,0)
        start=samples+8+rel
        assert start == end and start+frames*2 <= len(data)
        expected=b''.join(pcm[i:i+2] for i in range(channel*2,len(pcm),4))
        assert data[start:start+frames*2] == expected, 'CWAV PCM changed during packaging'
        end=start+frames*2
    assert end == len(data)
    return dict(channels=channels,rate=rate,frames=frames,pcm_matches_source=True)


def verify_launch_logo(data):
    assert len(data) == 0x2000, 'HOME launch splash must be exactly 8 KiB'
    splash=lz11(data)
    assert splash[:4] == b'darc', 'Invalid launch splash archive'
    assert struct.unpack_from('<HH',splash,4)==(0xfeff,0x1c)
    archive_size=u32(splash,12)
    assert archive_size+32==len(splash), 'Expected the splash archive and its 32-byte footer'
    table=u32(splash,16);count=u32(splash,table+8);names=table+count*12
    assert table==0x1c and 0<count<128 and names<archive_size
    entries={}
    for i in range(count):
        flags,offset,size=struct.unpack_from('<III',splash,table+i*12)
        start=end=names+(flags&0xffffff)
        while end+2<=archive_size and splash[end:end+2]!=b'\0\0':end+=2
        assert end+2<=archive_size
        name=splash[start:end].decode('utf-16le')
        if flags>>24:
            assert i<size<=count
        else:
            assert u32(splash,24)<=offset<offset+size<=archive_size
            entries[name]=splash[offset:offset+size]
    # The standard Homebrew splash retains HOME's upper/lower layout names.
    for screen in ('U','D'):
        name=f'NintendoLogo_{screen}_00.bclyt'
        assert entries[name][:4]==b'CLYT', f'Missing {screen} screen splash layout'
    return dict(bytes=len(data),decoded_bytes=len(splash),files=len(entries),
                both_screens=True,sha256=hashlib.sha256(data).hexdigest())


def verify(path, elf_path, art, *, cosmetic_baseline=None):
    raw = path.read_bytes()
    hdr, _, _, cert, ticket_size, tmd_size, meta_size, size = struct.unpack_from('<IHHIIIIQ', raw)
    assert hdr == 0x2020 and raw[0x20] == 0x80
    ticket_pos = align(hdr)+align(cert)
    tmd_pos = ticket_pos+align(ticket_size)
    content_pos = tmd_pos+align(tmd_size)
    assert content_pos+align(size)+meta_size == len(raw)
    ticket = raw[ticket_pos:ticket_pos+ticket_size]
    tmd = raw[tmd_pos:tmd_pos+tmd_size]
    content = raw[content_pos:content_pos+size]
    assert int.from_bytes(ticket[0x1dc:0x1e4], 'big') == TITLE_ID
    assert int.from_bytes(tmd[0x18c:0x194], 'big') == TITLE_ID
    assert int.from_bytes(tmd[0x1de:0x1e0], 'big') == 1
    chunk = tmd[0xb04:0xb34]
    assert int.from_bytes(chunk[8:16], 'big') == size
    assert not int.from_bytes(chunk[6:8], 'big') & 1
    assert hashlib.sha256(content).digest() == chunk[16:48]
    assert hashlib.sha256(tmd[0x204:0xb04]).digest() == tmd[0x1e4:0x204]
    assert content[0x100:0x104] == b'NCCH'
    for offset in (0x108,0x118,0x3c8,0x400,0x800):
        assert struct.unpack_from('<Q', content, offset)[0] == TITLE_ID, 'Inconsistent partition/program/jump ID'
    assert content[0x18c] == 2 and content[0x18f] & 4
    assert u32(content, 0x1b0) == 0, 'Unexpected embedded game assets/RomFS'
    exhdr = content[0x200:0x600]
    assert hashlib.sha256(exhdr).digest() == content[0x160:0x180]
    assert exhdr[0x20c:0x210] == bytes([3, 1, 4, 48]), 'CPU/memory/priority mismatch'
    services = [exhdr[i:i+8].rstrip(b'\0').decode() for i in range(0x250, 0x360, 8)]
    assert {'fs:USER', 'dsp::DSP', 'gsp::Gpu', 'hid:USER', 'APT:U', 'ir:rst', 'ptm:sysm'} <= set(services)
    offset, pages, hashed = struct.unpack_from('<III', content, 0x1a0)
    exefs = content[offset*512:(offset+pages)*512]
    assert hashlib.sha256(exefs[:hashed*512]).digest() == content[0x1c0:0x1e0]
    files = {}
    for i in range(10):
        name, off, count = struct.unpack_from('<8sII', exefs, i*16)
        if not count:
            continue
        data = exefs[512+off:512+off+count]
        assert hashlib.sha256(data).digest() == exefs[0xc0+(9-i)*32:0xe0+(9-i)*32]
        files[name.rstrip(b'\0').decode()] = data
    assert set(files) == {'.code', 'icon', 'banner', 'logo'}
    # HOME reads the boot splash independently of the selected-title banner.
    # A missing/wrong-sized splash can produce the misleading SD-removed error.
    launch_logo=verify_launch_logo(files['logo'])
    image = ElfImage(elf_path.read_bytes())
    code = blz(files['.code']) if exhdr[13] & 1 else files['.code']
    cursor = 0
    for p, off in zip(image.loads, (0x10, 0x20, 0x30)):
        address, pages, count = struct.unpack_from('<III', exhdr, off)
        assert address == p[2] and count == p[4]
        assert code[cursor:cursor+count] == image.data[p[1]:p[1]+count], 'Executable changed during CIA packing'
        cursor += pages*4096
    assert cursor == len(code)
    assert u32(exhdr, 0x3c) == image.loads[-1][5]-image.loads[-1][4]
    assert files['icon'] == (art/'icon.smdh').read_bytes()
    assert files['banner'] == (art/'banner.bin').read_bytes()
    icon, banner = files['icon'], files['banner']
    assert icon[:4] == b'SMDH' and len(icon) == 0x36c0
    assert u32(icon, 0x2018) == 0x7fffffff
    assert u32(icon, 0x2028) & 0x1025 == 0x1025, '3D/New3DS/extended-banner flags are required'
    assert banner[:4] == b'CBMD'
    cgfx = lz11(banner[u32(banner, 8):])
    assert cgfx == (art/'banner.cgfx').read_bytes()
    assert cgfx[:4] == b'CGFX' and len(cgfx) <= 0x80000
    baseline_model = (cosmetic_baseline/'banner.cgfx').read_bytes() if cosmetic_baseline else None
    baseline_container = (cosmetic_baseline/'banner.bin').read_bytes() if cosmetic_baseline else None
    verify_release_container(banner,cosmetic_baseline=baseline_container)
    banner_validation = verify_release_model(cgfx,cosmetic_baseline=baseline_model)
    sound = u32(banner, 0x84)
    assert sound % 16 == 0 and 0x88 < sound < len(banner)
    sound_validation = verify_sound(banner[sound:], art/'announcer.wav')
    return dict(title_id=f'{TITLE_ID:016X}', title_version=int.from_bytes(tmd[0x1dc:0x1de], 'big'), cia_bytes=len(raw),
                sha256=hashlib.sha256(raw).hexdigest(),
                code_bytes_verified=len(code), elf_sha256=hashlib.sha256(image.data).hexdigest(),
                cgfx_bytes=len(cgfx), banner_validation=banner_validation, sound_validation=sound_validation,
                smdh_flags=f'{u32(icon, 0x2028):08x}', launch_logo_bytes=len(files['logo']),
                launch_logo_sha256=hashlib.sha256(files['logo']).hexdigest(),
                launch_logo_validation=launch_logo,
                native_app=True, new_3ds_only=True,
                memory_mode='124MB', cpu_mhz=804, l2_cache=True,
                services=[s for s in services if s], embedded_game_romfs=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cia', type=Path)
    parser.add_argument('--elf', type=Path, default=Path('build/game-release/melee.elf'))
    parser.add_argument('--art', type=Path, default=Path('build/home-menu/art'))
    parser.add_argument('--cosmetic-baseline', type=Path)
    args = parser.parse_args()
    result = verify(args.cia, args.elf, args.art,cosmetic_baseline=args.cosmetic_baseline)
    args.cia.with_suffix('.verified.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
