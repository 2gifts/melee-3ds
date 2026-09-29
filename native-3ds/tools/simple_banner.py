"""A 2D HOME Menu banner, icon and banner sound made from the user's disc.

This is the easy route's art (tools/easy_build.py). The console-tested 3D
diorama needs an emulator capture and hand-made art; this needs only the
extracted disc files, Pillow, numpy and bannertool:

  banner  256x128: Melee's title-screen logo on the profile colour
  icon    48x48:   the Homebrew Launcher icon (tools/launcher_icon.py)
  sound   2.95 s:  the title call "Super Smash Bros. Melee" over the menu
                   theme, as in the diorama banner (tools/home_banner_audio.py)

bannertool's standard 2D banner template shows the image, as in most
homebrew CIAs. Nothing here is committed or redistributed.
"""
import subprocess
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

from assets import ROOT
from banner_assets import Archive, announcer_clips, texture
from home_banner_audio import menu_music
from launcher_icon import COLOURS, icon_image

RATE = 32000
SECONDS = 2.95
BANNERTOOL = ROOT/'.toolchain/home-menu/bannertool/windows-x86_64/bannertool.exe'


def logo_images():
    a = Archive(ROOT/'assets/GALE01/files/GmTitle.usd')
    # "SUPER SMASH BROS." filled and outlined, and "Melee" (banner_assets.inspect_title).
    fill, outline, melee = (texture(a, d).convert('RGBA') for d in (0x2724, 0x2470, 0x28b0))
    return fill, outline, melee


def white(image, colour=(255, 255, 255)):
    """An intensity texture as COLOUR with its intensity as alpha."""
    grey = image.convert('L')
    out = Image.new('RGBA', image.size, colour + (0,))
    out.putalpha(grey)
    return out


def banner_image(profile):
    top, bottom = {'unlocked': ((170, 26, 36), (40, 6, 10)), 'fresh': ((34, 80, 190), (8, 16, 52))}[profile]
    image = Image.new('RGBA', (256, 128))
    for y in range(128):
        t = y/127
        image.paste(tuple(round(a+(b-a)*t) for a, b in zip(top, bottom))+(255,), (0, y, 256, y+1))
    fill, outline, melee = logo_images()
    scale = 236/fill.width
    size = (236, round(fill.height*scale))
    logo = white(fill.resize(size, Image.Resampling.LANCZOS))
    edge = white(outline.resize(size, Image.Resampling.LANCZOS), (20, 20, 24))
    shadow = white(fill.resize(size, Image.Resampling.LANCZOS), (0, 0, 0)).filter(ImageFilter.GaussianBlur(2))
    x, y = 10, 8
    image.alpha_composite(shadow, (x+2, y+3))
    image.alpha_composite(edge, (x, y))
    image.alpha_composite(logo, (x, y))
    word_size = (round(melee.width*.62), round(melee.height*.62))
    word = white(melee.resize(word_size, Image.Resampling.LANCZOS))
    word_shadow = white(melee.resize(word_size, Image.Resampling.LANCZOS), (0, 0, 0)).filter(ImageFilter.GaussianBlur(2))
    wx, wy = 256-word_size[0]-14, y+size[1]+4
    image.alpha_composite(word_shadow, (wx+2, wy+2))
    image.alpha_composite(word, (wx, wy))
    return image.convert('RGB')


def title_call():
    """Clip 1 of nr_title.ssm, the title screen's "Super Smash Bros. Melee"."""
    pcm, rate = announcer_clips()[1]
    return np.array(pcm, float).T/32768, rate


def resample(x, rate, target):
    n = round(len(x)*target/rate)
    t = np.arange(n)*rate/target
    return np.stack([np.interp(t, np.arange(len(x)), x[:, c]) for c in range(x.shape[1])], 1)


def wsola(x, speed, frame=1024, search=256):
    """Waveform-similarity overlap-add: SPEED times faster, same pitch."""
    hop = frame//2
    window = np.hanning(frame)[:, None]
    mono = x.mean(1)
    count = int((len(x)-frame-search)/(hop*speed))
    out = np.zeros(((count+1)*hop+frame, x.shape[1]))
    norm = np.zeros((len(out), 1))
    previous = 0
    for k in range(count):
        centre = int(k*hop*speed)
        if k:
            # Continue the waveform: match the segment that follows the last one.
            want = mono[previous+hop:previous+hop+frame]
            lo, hi = max(0, centre-search), min(len(mono)-frame, centre+search)
            best, best_score = centre, -np.inf
            for s in range(lo, hi, 4):
                score = np.dot(want, mono[s:s+frame])
                if score > best_score:
                    best, best_score = s, score
            centre = best
        out[k*hop:k*hop+frame] += x[centre:centre+frame]*window
        norm[k*hop:k*hop+frame] += window
        previous = centre
    return out/np.maximum(norm, 1e-3)


def banner_sound(path):
    call, rate = title_call()
    call = call[round(2.43*rate):]
    voice = wsola(resample(call, rate, RATE), 1.455)
    frames = round(SECONDS*RATE)
    voice = np.pad(voice, ((0, max(0, frames-len(voice))), (0, 0)))[:frames]
    if voice.shape[1] == 1:
        voice = np.repeat(voice, 2, 1)
    fade = np.ones(frames)
    fade[:320] = np.linspace(0, 1, 320)
    fade[-1920:] = np.linspace(1, 0, 1920)
    voice *= fade[:, None]
    music, music_rate = menu_music(SECONDS)
    theme = resample(np.array(music, float).T/32768, music_rate, RATE)[:frames]
    ramp = np.ones(frames)
    ramp[:640] = np.linspace(0, 1, 640)
    ramp[-2560:] = np.linspace(1, 0, 2560)
    voice *= .80/max(abs(voice).max(), 1e-9)
    theme *= .32/max(abs(theme).max(), 1e-9)
    mix = voice+theme*ramp[:, None]
    mix *= min(1, .95/max(abs(mix).max(), 1e-9))
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(np.rint(mix*32767).astype('<i2').tobytes())


def make_art(profile, out=None):
    """Write banner.png, icon.png, announcer.wav, banner.bin and banner.cgfx."""
    from verify_cia import lz11, u32
    out = Path(out or ROOT/f'build/home-menu/generated-{profile}')
    out.mkdir(parents=True, exist_ok=True)
    banner_image(profile).save(out/'banner.png')
    icon_image(profile).convert('RGB').save(out/'icon.png')
    banner_sound(out/'announcer.wav')
    subprocess.run([str(BANNERTOOL), 'makebanner', '-i', str(out/'banner.png'), '-a', str(out/'announcer.wav'),
                    '-o', str(out/'banner.bin')], check=True, capture_output=True)
    packed = (out/'banner.bin').read_bytes()
    assert packed[:4] == b'CBMD'
    # verify_cia compares the packed model with banner.cgfx.
    (out/'banner.cgfx').write_bytes(lz11(packed[u32(packed, 8):]))
    return out


if __name__ == '__main__':
    import sys
    for name in sys.argv[1:] or COLOURS:
        print(make_art(name))
