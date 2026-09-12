"""Check the complete-banner release lock against local working/failed archives."""
import argparse
from pathlib import Path
from home_banner_release import verify_release_model, verify_release_container


def rejected(label,fn,data):
    try:
        fn(data)
    except AssertionError:
        print('PASS: rejects '+label)
    else:
        raise AssertionError('Accepted '+label)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--approved-art',type=Path,required=True)
    ap.add_argument('--failed-art',type=Path,action='append',default=[])
    args = ap.parse_args()
    cgfx = (args.approved_art/'banner.cgfx').read_bytes()
    cbmd = (args.approved_art/'banner.bin').read_bytes()
    verify_release_model(cgfx);verify_release_container(cbmd)
    print('PASS: exact console-tested complete banner')
    for folder in args.failed_art:
        rejected(str(folder)+' model',verify_release_model,(folder/'banner.cgfx').read_bytes())
        rejected(str(folder)+' container',verify_release_container,(folder/'banner.bin').read_bytes())
    # A model can be unchanged while its compressed container/audio differs.
    for label,position in [('compressed model',160),('audio',len(cbmd)-8),('container padding',7)]:
        bad = bytearray(cbmd);bad[position]^=1
        rejected(label,verify_release_container,bytes(bad))


if __name__=='__main__':
    main()
