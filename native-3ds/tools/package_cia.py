"""Package the native game as a locally built, installable New 3DS CIA.

Requires a finalized BE8 ELF and locally authored banner art. It never reads
an account key, embeds the game's filesystem, or contacts a console.
"""
import argparse
import json
import subprocess
import wave
from pathlib import Path

from assets import ROOT
from be8_image import ElfImage
from verify_cia import verify
from verify_home_banner import verify_banner


def run(*args):
    subprocess.run([str(a) for a in args], check=True, cwd=ROOT)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, default=ROOT/'build/game-release/melee.elf')
    ap.add_argument('--art', type=Path, default=ROOT/'build/home-menu/art')
    ap.add_argument('--output', type=Path, default=ROOT/'dist/home-menu/melee-3ds.cia')
    ap.add_argument('--development', action='store_true', help='Allow an emulator-only validation ELF')
    ap.add_argument('--version', type=int, default=6, help='CIA title version; retains the installed title ID')
    ap.add_argument('--cci', type=Path, help='Also emit a local emulator test cartridge')
    args = ap.parse_args()
    image = ElfImage(args.elf.read_bytes())
    if not args.development:
        forbidden = {'mp_test_control', 'mp_test_capture', 'mp_banner_capture', 'mp_test_stereo_slider'}
        assert not forbidden & image.symbols.keys(), 'Development ELF cannot be distributed as the console build'
    # Reject known unsafe profiles before writing any installable output.
    verify_banner((args.art/'banner.cgfx').read_bytes())
    with wave.open(str(args.art/'announcer.wav'), 'rb') as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        assert 0 < wav.getnframes()/wav.getframerate() <= 3
    bannertool = ROOT/'.toolchain/home-menu/bannertool/windows-x86_64/bannertool.exe'
    makerom = ROOT/'.toolchain/home-menu/makerom/makerom.exe'
    run(bannertool, 'makesmdh', '-s', 'Super Smash Bros. Melee',
        '-l', 'Melee - Native New Nintendo 3DS port', '-p', '2gifts and contributors',
        '-i', args.art/'icon.png', '-o', args.art/'icon.smdh', '-r', 'regionfree',
        '-f', 'visible,allow3d,new3ds,recordusage,extendedbanner')
    run(bannertool, 'makebanner', '-ci', args.art/'banner.cgfx',
        '-a', args.art/'announcer.wav', '-o', args.art/'banner.bin')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    common = ['-target', 't', '-exefslogo', '-elf', args.elf, '-rsf', ROOT/'port/3ds/melee.rsf',
              '-banner', args.art/'banner.bin', '-icon', args.art/'icon.smdh']
    run(makerom, '-f', 'cia', *common, '-ver', args.version, '-o', args.output)
    result = verify(args.output, args.elf, args.art)
    result['development_only'] = args.development
    args.output.with_suffix('.verified.json').write_text(json.dumps(result, indent=2)+'\n')
    if args.cci:
        args.cci.parent.mkdir(parents=True, exist_ok=True)
        run(makerom, '-f', 'cci', *common, '-o', args.cci)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
