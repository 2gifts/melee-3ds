"""Apply cosmetic changes without rebuilding the console-tested CGFX layout.

Only reverse each Fox triangle and replace the existing RGBA4 logo pixels.
Resource pointers, sizes, vertex data, bones, animation and render state stay
byte-identical. The packed banner retains its original header and audio offset.
This bounds the change; it does not substitute for testing on physical HOME.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from verify_home_banner import Reader, verify_banner


def sha(data):
    return hashlib.sha256(data).hexdigest()


def cosmetic_ranges(data):
    r = Reader(data)
    model = r.dictionary(28)['COMMON']
    indices = []
    for mesh in r.array(model+180):
        if r.string(mesh+112) not in ('Fox 1', 'Fox 2'):
            continue
        shape = r.array(model+196)[r.u32(mesh+24)]
        for ps in r.array(shape+44):
            for primitive in r.array(ps+12):
                for stream in r.array(primitive):
                    assert r.u32(stream) == 0x1403
                    start, size = r.ptr(stream+12), r.u32(stream+8)
                    assert size % 6 == 0
                    indices.append((start, size))
    assert len(indices) == 2
    logos = [t for t in r.dictionary(36).values() if r.read('II',t+24)==(128,256)]
    assert len(logos) == 1 and r.u32(logos[0]+52) == 4
    pixel = r.ptr(logos[0]+56)
    logo = r.ptr(pixel+12), r.u32(pixel+8)
    assert logo[1] == 256*128*2
    return indices, logo


def reverse_triangles(data, ranges):
    result = bytearray(data)
    for start, size in ranges:
        for off in range(start,start+size,6):
            result[off+2:off+6] = data[off+4:off+6]+data[off+2:off+4]
    return result


def verify_cosmetic_model(data, baseline):
    from home_banner_release import APPROVED_CGFX
    assert sha(baseline) == APPROVED_CGFX, 'Requires the exact console-tested model'
    assert len(data) == len(baseline)
    indices, (start, size) = cosmetic_ranges(baseline)
    expected = reverse_triangles(baseline,indices)
    # Pixels are the only freely replaceable data. The index changes must be
    # exactly the original triangles with their last two vertices exchanged.
    expected[start:start+size] = data[start:start+size]
    assert data == expected, 'Change outside the permitted winding/logo pixels'
    result = verify_banner(data)
    result.update(console_tested=False, release_baseline='console-tested package 5',
                  patch='in-place winding and logo pixels', layout_identical=True,
                  baseline_sha256=sha(baseline), changed_bytes=sum(a!=b for a,b in zip(data,baseline)),
                  triangle_counts=[size//6 for _,size in indices])
    return result


def preserve_container(packed, baseline):
    from home_banner_release import APPROVED_CBMD
    from verify_cia import lz11, u32
    assert sha(baseline) == APPROVED_CBMD, 'Requires the exact console-tested CBMD'
    assert packed[:4] == b'CBMD' and packed[:0x84] == baseline[:0x84]
    start, sound = u32(baseline,8), u32(baseline,0x84)
    data, consumed = lz11(packed[start:],return_consumed=True)
    assert start+consumed < sound, 'New compressed model does not fit the original slot'
    assert packed[u32(packed,0x84):] == baseline[sound:], 'Audio changed'
    result = bytearray(baseline)
    result[start:sound] = packed[start:start+consumed]+bytes(sound-start-consumed)
    verify_cosmetic_container(bytes(result),baseline)
    return bytes(result)


def verify_cosmetic_container(data, baseline):
    from home_banner_release import APPROVED_CBMD
    from verify_cia import lz11, u32
    assert sha(baseline) == APPROVED_CBMD
    assert len(data) == len(baseline) and data[:0x88] == baseline[:0x88], 'CBMD layout changed'
    start, sound = u32(baseline,8), u32(baseline,0x84)
    assert data[sound:] == baseline[sound:], 'CWAV bytes or location changed'
    model, consumed = lz11(data[start:sound],return_consumed=True)
    assert not any(data[start+consumed:sound]), 'Nonzero compressed-stream padding'
    result = verify_cosmetic_model(model,lz11(baseline[start:sound]))
    result.update(cbmd_bytes=len(data),cwav_offset=sound,cwav_identical=True,
                  compressed_bytes=consumed,compressed_capacity=sound-start)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline-art',type=Path,required=True)
    ap.add_argument('--output-art',type=Path,required=True)
    ap.add_argument('--logo',type=Path,required=True,help='Locally extracted 512x256 logo composition')
    args = ap.parse_args()
    assert args.output_art.resolve() != args.baseline_art.resolve()
    from home_banner_release import verify_release_model, verify_release_container
    from home_banner_texture import prepare_logo
    from PIL import Image
    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root/'.toolchain/home-menu/python'),str(root/'.toolchain/home-menu/pycgfx')]
    from cgfx.swizzler import swizzle
    from cgfx.txob import TextureFormat
    baseline = (args.baseline_art/'banner.cgfx').read_bytes()
    container = (args.baseline_art/'banner.bin').read_bytes()
    verify_release_model(baseline); verify_release_container(container)
    indices,(start,size) = cosmetic_ranges(baseline)
    data = reverse_triangles(baseline,indices)
    image = prepare_logo(Image.open(args.logo))
    pixels = swizzle(image.transpose(Image.Transpose.FLIP_TOP_BOTTOM),TextureFormat.RGBA4)
    assert len(pixels) == size
    data[start:start+size] = pixels
    verify_cosmetic_model(bytes(data),baseline)
    args.output_art.mkdir(parents=True,exist_ok=True)
    for name in ('icon.smdh','icon.png','announcer.wav','diorama-atlas.png'):
        shutil.copyfile(args.baseline_art/name,args.output_art/name)
    shutil.copyfile(args.logo,args.output_art/'logo.png')
    # Retain the original texture resource name without touching its dictionary.
    r = Reader(baseline)
    name = next(name for name,t in r.dictionary(36).items() if r.u32(t+24)==128)
    image.save(args.output_art/name)
    (args.output_art/'banner.cgfx').write_bytes(data)
    temporary = args.output_art/'recompressed.bin'
    subprocess.run([str(root/'.toolchain/home-menu/bannertool/windows-x86_64/bannertool.exe'),
                    'makebanner','-ci',str(args.output_art/'banner.cgfx'),'-a',
                    str(args.output_art/'announcer.wav'),'-o',str(temporary)],check=True)
    packed = preserve_container(temporary.read_bytes(),container)
    (args.output_art/'banner.bin').write_bytes(packed)
    result = verify_cosmetic_container(packed,container)
    result.update(cgfx_sha256=sha(data),cbmd_sha256=sha(packed))
    (args.output_art/'cosmetic-patch.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
