"""Homebrew Launcher title and icon for each save profile.

The icon is drawn from the game's own GameCube banner (opening.bnr from the
user's disc files) on a profile colour: red for everything unlocked, blue
for the fresh save. Nothing is written if the banner, Pillow or bannertool
is unavailable; the launcher then shows the file name.
"""
import struct
import subprocess
from build import ROOT

TITLES = {
    'unlocked': ('Super Smash Bros. Melee', 'Everything unlocked - saves in 3ds/melee/saves/unlocked'),
    'fresh': ('Melee: Fresh Save', 'Unlock everything by playing - saves in 3ds/melee/saves/fresh'),
}
COLOURS = {'unlocked': (176, 24, 32), 'fresh': (28, 72, 176)}
BANNERS = [
    ROOT / 'assets/GALE01/files/opening.bnr',
    ROOT / 'build/disc/files/opening.bnr',
    ROOT / '.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee/files/opening.bnr',
]


def banner_image():
    from PIL import Image
    path = next((p for p in BANNERS if p.exists()), None)
    if not path:
        return None
    data = path.read_bytes()
    if data[:4] not in (b'BNR1', b'BNR2'):
        return None
    image = Image.new('RGBA', (96, 32))
    pixels = image.load()
    offset = 0x20
    for ty in range(0, 32, 4):
        for tx in range(0, 96, 4):
            for y in range(4):
                for x in range(4):
                    v = struct.unpack_from('>H', data, offset)[0]
                    offset += 2
                    if v & 0x8000:
                        c = ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
                    else:
                        c = ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)
                    pixels[tx + x, ty + y] = c
    return image


def icon_image(profile, banner=None):
    """48x48: the disc banner's logo on a card in the profile colour."""
    from PIL import Image
    banner = banner or banner_image()
    icon = Image.new('RGBA', (48, 48), COLOURS[profile] + (255,))
    frame = Image.new('RGBA', (46, 46), (16, 16, 16, 255))
    icon.alpha_composite(frame, (1, 1))
    logo = banner.resize((46, 15), Image.Resampling.LANCZOS)
    icon.alpha_composite(logo, (1, 8))
    band = Image.new('RGBA', (46, 12), COLOURS[profile] + (255,))
    icon.alpha_composite(band, (1, 30))
    return icon


LABELS = {'unlocked': (206, 18, 22), 'fresh': (30, 74, 186)}


def disc_icon(profile, size=48):
    """A Melee disc drawn from the game's title lettering (GmTitle.usd): the
    label in the profile colour behind the Smash emblem, silver hub and rim."""
    from PIL import Image, ImageDraw, ImageFilter
    from banner_assets import Archive, texture
    title = Archive(ROOT/'assets/GALE01/files/GmTitle.usd')
    n, c = 1024, 512
    icon = Image.new('RGBA', (n, n), (238, 240, 244, 255))
    pen = ImageDraw.Draw(icon)
    pen.ellipse((c-500, c-500, c+500, c+500), fill=(196, 199, 205, 255))
    pen.ellipse((c-488, c-488, c+488, c+488), fill=(226, 228, 232, 255))
    label = Image.new('RGBA', (n, n), LABELS[profile]+(255,))
    art = ImageDraw.Draw(label)
    # The emblem: a disc cut by a vertical and a horizontal bar.
    # Below its horizontal bar the printed emblem is a dark half-tone.
    shade = tuple(v*2//5 for v in LABELS[profile])+(255,)
    art.ellipse((c-372, 92, c+372, 836), fill=(14, 14, 16, 255))
    art.rectangle((0, 530, n, n), fill=LABELS[profile]+(255,))
    art.pieslice((c-372, 92, c+372, 836), 0, 180, fill=shade)
    art.rectangle((0, 470, n, 530), fill=LABELS[profile]+(255,))
    art.rectangle((c-236, 0, c-170, 470), fill=LABELS[profile]+(255,))
    art.rectangle((0, 626, n, n), fill=(14, 14, 16, 255))
    mask = Image.new('L', (n, n))
    ImageDraw.Draw(mask).ellipse((c-478, c-478, c+478, c+478), fill=255)
    icon.paste(label, (0, 0), mask)

    def lettering(descriptor, width, top, colour, edge):
        image = texture(title, descriptor).convert('L')
        image = image.resize((width, round(image.height*width/image.width)), Image.Resampling.LANCZOS)
        outline = image.filter(ImageFilter.MaxFilter(15))
        x = c-width//2
        icon.paste(Image.new('RGBA', image.size, edge+(255,)), (x, top), outline)
        icon.paste(Image.new('RGBA', image.size, colour+(255,)), (x, top), image)
    lettering(0x2724, 700, 96, (244, 244, 246), (20, 20, 24))
    lettering(0x28b0, 330, 318, (30, 30, 34), (226, 228, 232))
    pen = ImageDraw.Draw(icon)
    pen.ellipse((c-150, c-150, c+150, c+150), fill=(214, 216, 222, 255), outline=(150, 152, 160, 255), width=8)
    pen.ellipse((c-104, c-104, c+104, c+104), fill=(238, 240, 244, 255), outline=(170, 172, 180, 255), width=6)
    return icon.convert('RGB').resize((size, size), Image.Resampling.LANCZOS)


def make_smdh(out, profile):
    bannertool = ROOT / '.toolchain/home-menu/bannertool/windows-x86_64/bannertool.exe'
    try:
        banner = banner_image()
    except ImportError:
        return None
    if banner is None or not bannertool.exists():
        return None
    icon = icon_image(profile, banner)
    png = out / f'launcher-{profile}.png'
    smdh = out / f'launcher-{profile}.smdh'
    icon.convert('RGB').save(png)
    short, long = TITLES[profile]
    subprocess.run([str(bannertool), 'makesmdh', '-s', short, '-l', long, '-p', 'Native New 3DS port',
                    '-i', str(png), '-o', str(smdh), '-r', 'regionfree',
                    '-f', 'visible,allow3d,new3ds,recordusage'], check=True, capture_output=True)
    return smdh
