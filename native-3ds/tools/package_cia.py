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
from verify_cia import verify, lz11, u32
from home_banner_release import verify_release_model, verify_release_container


# Each save profile is its own HOME Menu title, so both can be installed.
PROFILES = {
    'unlocked': dict(elf='build/game-release/melee.elf', output='dist/home-menu/melee-3ds.cia',
                     short='Super Smash Bros. Melee', long='Melee - Native New Nintendo 3DS port',
                     name='Melee3DS', product='CTR-P-M3LE', unique=0xF4D45),
    'fresh': dict(elf='build/game-release-fresh/melee.elf', output='dist/home-menu/melee-3ds-fresh.cia',
                  short='Melee: Fresh Save', long='Melee - Fresh save, unlock everything by playing',
                  name='Melee3DSFresh', product='CTR-P-M3LF', unique=0xF4D46),
}


def fresh_art(art):
    """The fresh-save title shares the banner; its icon's red disc label
    turns blue, the profile colour of its Homebrew Launcher entry."""
    import shutil
    from PIL import Image
    out = ROOT/'build/home-menu/art-fresh'
    out.mkdir(parents=True, exist_ok=True)
    for name in ('banner.cgfx', 'announcer.wav', 'banner.bin'):
        if (art/name).exists():
            shutil.copyfile(art/name, out/name)
    icon = Image.open(art/'icon.png').convert('RGB')
    icon.putdata([(b, g, r) if r > g + 40 and r > b + 40 else (r, g, b) for r, g, b in icon.getdata()])
    icon.save(out/'icon.png')
    return out


def run(*args):
    subprocess.run([str(a) for a in args], check=True, cwd=ROOT)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=PROFILES, default='unlocked',
                    help='fresh: the fresh-save build as a separate HOME Menu title')
    ap.add_argument('--elf', type=Path)
    ap.add_argument('--art', type=Path, default=ROOT/'build/home-menu/art')
    ap.add_argument('--output', type=Path)
    ap.add_argument('--development', action='store_true', help='Allow an emulator-only validation ELF')
    ap.add_argument('--version', type=int, default=7, help='CIA title version; retains the installed title ID')
    ap.add_argument('--cci', type=Path, help='Also emit a local emulator test cartridge')
    ap.add_argument('--cosmetic-baseline', type=Path,
                    help='Explicit candidate: verify an in-place patch against this console-tested art directory')
    args = ap.parse_args()
    profile = PROFILES[args.profile]
    args.elf = args.elf or ROOT/profile['elf']
    args.output = args.output or ROOT/profile['output']
    title_id = 0x0004000000000000 | profile['unique'] << 8
    if args.profile == 'fresh':
        args.art = fresh_art(args.art)
    rsf = ROOT/'port/3ds/melee.rsf'
    if args.profile != 'unlocked':
        text = rsf.read_text(encoding='utf-8')
        for key, old, new in (('Title', 'Melee3DS', profile['name']), ('ProductCode', 'CTR-P-M3LE', profile['product']),
                              ('UniqueId', '0xF4D45', f"0x{profile['unique']:X}")):
            assert text.count(f'{key}: {old}') == 1
            text = text.replace(f'{key}: {old}', f'{key}: {new}')
        rsf = args.art/'melee.rsf'
        rsf.write_text(text, encoding='utf-8')
    image = ElfImage(args.elf.read_bytes())
    if not args.development:
        forbidden = {'mp_test_control', 'mp_test_capture', 'mp_banner_capture', 'mp_test_stereo_slider',
                     'mp_probe_request', 'mp_probe_rows'}
        assert not forbidden & image.symbols.keys(), 'Development ELF cannot be distributed as the console build'
    # Reject known unsafe profiles before writing any installable output.
    baseline_model = (args.cosmetic_baseline/'banner.cgfx').read_bytes() if args.cosmetic_baseline else None
    baseline_container = (args.cosmetic_baseline/'banner.bin').read_bytes() if args.cosmetic_baseline else None
    verify_release_model((args.art/'banner.cgfx').read_bytes(),cosmetic_baseline=baseline_model)
    with wave.open(str(args.art/'announcer.wav'), 'rb') as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        assert 0 < wav.getnframes()/wav.getframerate() <= 3
    bannertool = ROOT/'.toolchain/home-menu/bannertool/windows-x86_64/bannertool.exe'
    makerom = ROOT/'.toolchain/home-menu/makerom/makerom.exe'
    run(bannertool, 'makesmdh', '-s', profile['short'],
        '-l', profile['long'], '-p', '2gifts and contributors',
        '-i', args.art/'icon.png', '-o', args.art/'icon.smdh', '-r', 'regionfree',
        '-f', 'visible,allow3d,new3ds,recordusage,extendedbanner')
    if args.cosmetic_baseline:
        # patch_home_banner.py has already recompressed into the original slot.
        # Running makebanner again would move the unchanged sound resource.
        # The fresh-save title's icon differs by design; its banner does not.
        if args.profile == 'unlocked':
            assert (args.art/'icon.smdh').read_bytes() == (args.cosmetic_baseline/'icon.smdh').read_bytes()
    else:
        run(bannertool, 'makebanner', '-ci', args.art/'banner.cgfx',
            '-a', args.art/'announcer.wav', '-o', args.art/'banner.bin')
    verify_release_container((args.art/'banner.bin').read_bytes(),cosmetic_baseline=baseline_container)
    packed = (args.art/'banner.bin').read_bytes()
    assert lz11(packed[u32(packed,8):]) == (args.art/'banner.cgfx').read_bytes(), 'Packed model differs from verified art'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    common = ['-target', 't', '-exefslogo', '-elf', args.elf, '-rsf', rsf,
              '-banner', args.art/'banner.bin', '-icon', args.art/'icon.smdh']
    run(makerom, '-f', 'cia', *common, '-ver', args.version, '-o', args.output)
    result = verify(args.output, args.elf, args.art,cosmetic_baseline=args.cosmetic_baseline,title_id=title_id)
    result['development_only'] = args.development
    args.output.with_suffix('.verified.json').write_text(json.dumps(result, indent=2)+'\n')
    if args.cci:
        args.cci.parent.mkdir(parents=True, exist_ok=True)
        run(makerom, '-f', 'cci', *common, '-o', args.cci)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
