"""Reissue a locally archived working CIA without rebuilding any application content.

Requires the exact package 5 archive confirmed on the owner's New 3DS. The
newer TMD version permits a normal update over the withdrawn packages. No
assets or console keys are embedded in this script.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from assets import ROOT
from verify_cia import align, verify

APPROVED_CIA = '08b5120195f13cc446eb2fa0a9be666a542900f4e155c76945a1faa69e2ff15e'
APPROVED_CONTENT = '10bc09ac7d64f0c2f8f7399b91dec82a62d15c00a6067ad8c1b89acfd1fd1f0b'


def read_content(raw):
    hdr,_,_,cert,ticket,tmd,meta,size = struct.unpack_from('<IHHIIIIQ',raw)
    assert hdr==0x2020
    start = align(hdr)+align(cert)+align(ticket)+align(tmd)
    assert start+align(size)+meta==len(raw)
    return raw[start:start+size]


def restore(source, elf, art, output, version):
    raw = source.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==APPROVED_CIA, 'Not the confirmed package 5 archive'
    baseline = verify(source,elf,art)
    content = read_content(raw)
    assert hashlib.sha256(content).hexdigest()==APPROVED_CONTENT
    assert 7<=version<=65535 and version>baseline['title_version']
    output = output.resolve()
    assert output != source.resolve(), 'Preserve the working archive'
    output.parent.mkdir(parents=True,exist_ok=True)
    # Stage and verify before replacing the normal installation file.
    with tempfile.TemporaryDirectory(prefix='home-restore-',dir=output.parent) as temp:
        temp = Path(temp)
        ncch,candidate = temp/'application.cxi',temp/'restored.cia'
        ncch.write_bytes(content)
        makerom = ROOT/'.toolchain/home-menu/makerom/makerom.exe'
        subprocess.run([str(makerom),'-f','cia','-target','t','-content',
                        str(ncch)+':0:0','-ver',str(version),'-o',str(candidate)],check=True)
        result = verify(candidate,elf,art)
        assert result['title_version']==version
        assert read_content(candidate.read_bytes())==content, 'Application content changed during reissue'
        result.update(package='HOME package 8 recovery',
                      content_identical_to_console_tested_package5=True,
                      application_content_sha256=APPROVED_CONTENT,
                      source_cia_sha256=APPROVED_CIA,
                      reinstalled_on_physical_console=False)
        candidate.replace(output)
    output.with_suffix('.verified.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True,help='Locally archived confirmed package 5 CIA')
    ap.add_argument('--art',type=Path,required=True,help='Matching archived package 5 art directory')
    ap.add_argument('--elf',type=Path,default=ROOT/'build/game-release/melee.elf')
    ap.add_argument('--output',type=Path,default=ROOT/'dist/home-menu/melee-3ds.cia')
    ap.add_argument('--version',type=int,default=7)
    args = ap.parse_args()
    print(json.dumps(restore(args.source,args.elf,args.art,args.output,args.version),indent=2))


if __name__=='__main__':
    main()
