"""Regression checks for the separate HOME launch resource, using a local CIA."""
import argparse
import struct
from pathlib import Path
from verify_cia import align, verify_launch_logo


def read_logo(path):
    raw=path.read_bytes()
    hdr,_,_,cert,ticket,tmd,_,size=struct.unpack_from('<IHHIIIIQ',raw)
    start=align(hdr)+align(cert)+align(ticket)+align(tmd)
    ncch=raw[start:start+size]
    exefs_offset,exefs_size=struct.unpack_from('<II',ncch,0x1a0)
    exefs=ncch[exefs_offset*512:(exefs_offset+exefs_size)*512]
    for i in range(10):
        name,offset,count=struct.unpack_from('<8sII',exefs,i*16)
        if name.rstrip(b'\0')==b'logo':return exefs[512+offset:512+offset+count]
    return b''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cia',nargs='?',type=Path,default=Path('dist/home-menu/melee-3ds.cia'))
    ap.add_argument('--previous',type=Path,help='Optional earlier CIA with the missing launch logo')
    args=ap.parse_args();logo=read_logo(args.cia)
    print('PASS: launch splash',verify_launch_logo(logo))
    cases=[('missing',b''),('truncated',logo[:-512]),('oversized',logo+bytes(512)),
           ('invalid archive',bytes(8192))]
    if args.previous:cases.append(('previous package',read_logo(args.previous)))
    for name,data in cases:
        try:verify_launch_logo(data)
        except AssertionError:print('PASS: rejects',name,'launch splash')
        else:raise AssertionError(f'Accepted {name}')


if __name__=='__main__':main()
