"""Check bounded banner updates against local known-good and failed fixtures."""
import argparse
from pathlib import Path
from patch_home_banner import cosmetic_ranges, verify_cosmetic_model, verify_cosmetic_container
from home_banner_release import verify_release_model, verify_release_container
from verify_cia import lz11, u32


def rejects(name, check):
    try:
        check()
    except (AssertionError,IndexError,ValueError):
        print('PASS: rejects',name)
    else:
        raise AssertionError('Accepted '+name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',type=Path,required=True)
    ap.add_argument('--candidate',type=Path,required=True)
    ap.add_argument('--failed',type=Path,action='append',default=[])
    args = ap.parse_args()
    base = (args.baseline/'banner.cgfx').read_bytes()
    old_container = (args.baseline/'banner.bin').read_bytes()
    candidate = (args.candidate/'banner.cgfx').read_bytes()
    container = (args.candidate/'banner.bin').read_bytes()
    verify_cosmetic_model(candidate,base)
    verify_cosmetic_container(container,old_container)
    rejects('implicit promotion to console-confirmed model',lambda:verify_release_model(candidate))
    rejects('implicit promotion to console-confirmed CBMD',lambda:verify_release_container(container))
    indices,logo = cosmetic_ranges(base)
    for name,off in [('resource header',24),('index value',indices[0][0]),('vertex data',indices[0][0]+indices[0][1]+32)]:
        bad = bytearray(candidate); bad[off] ^= 1
        rejects(name,lambda:verify_cosmetic_model(bytes(bad),base))
    rejects('original inward-facing triangles',lambda:verify_cosmetic_model(base,base))
    _,used = lz11(container[u32(container,8):],return_consumed=True)
    for name,off in [('sound offset',0x84),('sound samples',len(container)-1),('compressed padding',u32(container,8)+used)]:
        bad = bytearray(container); bad[off] ^= 1
        rejects(name,lambda:verify_cosmetic_container(bytes(bad),old_container))
    for failed in args.failed:
        rejects(str(failed)+' model',lambda:verify_cosmetic_model((failed/'banner.cgfx').read_bytes(),base))
        rejects(str(failed)+' container',lambda:verify_cosmetic_container((failed/'banner.bin').read_bytes(),old_container))
    print('PASS: candidate alters only the specified indices/pixels and preserves CBMD header/audio/size')


if __name__ == '__main__':
    main()
