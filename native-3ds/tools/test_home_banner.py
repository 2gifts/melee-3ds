"""Reject crash-prone/corrupted variants of a locally generated banner."""
import argparse
import struct
from pathlib import Path

from verify_home_banner import Reader, verify_banner
from verify_cia import verify_sound


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('banner', nargs='?', type=Path, default=Path('build/home-menu/art/banner.cgfx'))
    ap.add_argument('--previous', type=Path, help='Optional archived unsafe banner regression fixture')
    args = ap.parse_args()
    data = args.banner.read_bytes()
    verify_banner(data)
    r = Reader(data)
    model = r.dictionary(28)['COMMON']
    mesh = r.array(model+180)[0]
    shape = r.array(model+196)[r.u32(mesh+24)]
    primitive = r.array(shape+44)[0]
    attribute = r.array(shape+56)[0]
    skeleton = r.ptr(model+224)
    bones = r.dictionary(skeleton+24)
    material = next(iter(r.dictionary(model+188).values()))
    fragment = r.ptr(material+648)
    animation = r.dictionary(100)['COMMON']
    member = next(iter(r.dictionary(animation+24).values()))
    logo_texture = next(t for t in r.dictionary(36).values() if r.u32(t+28)==512)
    cases = [
        ('soft skin', primitive+8, 2),
        ('bone-index stream', attribute+4, 7),
        ('missing mesh binding', mesh+112, 0),
        ('non-mesh animation', member+4, r.ptr(bones['Scene root'])-(member+4)),
        ('baked animation', member+16, 8),
        ('logo transform', bones['Melee logo']+56, 0x3f800000),
        ('billboard fighter', bones['Fox 1']+212, 5),
        ('downsampled logo', logo_texture+28, 256),
        ('wrong logo pixel format', logo_texture+52, 4),
        ('dangling shape array', model+200, 0x7fffffff),
        ('quantized vertex stream', attribute+36, 0x1402),
        ('nonfinite vertex', r.ptr(attribute+24), 0x7fc00000),
        ('incomplete transform tracks', member, 0x3f0000),
        ('custom unlit material', material+24, 0),
        ('custom culling command', material+272, 0),
        ('HOME-light-dependent colors', fragment+44+28+4, 0x0fff00f1),
    ]
    for name, offset, value in cases:
        bad = bytearray(data)
        struct.pack_into('<I', bad, offset, value & 0xffffffff)
        try:
            verify_banner(bytes(bad))
        except (AssertionError, UnicodeError, KeyError, IndexError):
            print(f'PASS: rejects {name}')
        else:
            raise AssertionError(f'Accepted {name}')
    # Reverse a whole fighter's winding, reproducing the hollow-face defect.
    bad = bytearray(data)
    fox_mesh = next(m for m in r.array(model+180) if r.string(m+112)=='Fox 1')
    fox_shape = r.array(model+196)[r.u32(fox_mesh+24)]
    for ps in r.array(fox_shape+44):
        for p in r.array(ps+12):
            for stream in r.array(p):
                width = 1 if r.u32(stream)==0x1401 else 2
                start,size = r.ptr(stream+12),r.u32(stream+8)
                for i in range(start,start+size,3*width):
                    bad[i+width:i+3*width] = bad[i+2*width:i+3*width]+bad[i+width:i+2*width]
    try:
        verify_banner(bytes(bad))
    except AssertionError as error:
        assert 'Inside-out' in str(error)
        print('PASS: rejects inside-out fighter')
    else:
        raise AssertionError('Accepted inside-out fighter')
    if args.previous:
        try:
            verify_banner(args.previous.read_bytes())
        except AssertionError as error:
            print(f'PASS: rejects previous package: {error}')
        else:
            raise AssertionError('Previous unsafe banner was accepted')
    packed=args.banner.with_name('banner.bin')
    wav=args.banner.with_name('announcer.wav')
    if packed.exists() and wav.exists():
        banner=packed.read_bytes()
        sound=banner[struct.unpack_from('<I',banner,0x84)[0]:]
        verify_sound(sound,wav)
        info=struct.unpack_from('<I',sound,24)[0]
        entry=info+28+struct.unpack_from('<I',sound,info+36)[0]
        for name,off in [('sound size',12),('sample count',info+20),('channel pointer',entry+4)]:
            bad=bytearray(sound)
            struct.pack_into('<I',bad,off,0x7fffffff)
            try:
                verify_sound(bad,wav)
            except (AssertionError,struct.error):
                print(f'PASS: rejects invalid {name}')
            else:
                raise AssertionError(f'Accepted invalid {name}')
    print('PASS: current serialized banner')


if __name__ == '__main__':
    main()
