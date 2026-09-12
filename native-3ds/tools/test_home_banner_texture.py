"""Check logo packing against an independent Morton-address decoder."""
from pathlib import Path
from PIL import Image
from home_banner_texture import encode_la4
from verify_home_banner import Reader


def check_pixels(data, width, height, image):
    pixels = image.convert('RGBA').load()
    seen = set()
    for y in range(height):
        for x in range(width):
            # Independent bit-mask expansion, including vertical inversion.
            yy = height-1-y
            mx = (x&1) | (x&2)<<1 | (x&4)<<2
            my = (yy&1)<<1 | (yy&2)<<2 | (yy&4)<<3
            offset = (yy//8*width//8+x//8)*64+mx+my
            seen.add(offset)
            sample = data[offset]
            r,g,b,a = pixels[x,y]
            assert (sample>>4,sample&15) == (r>>4,a>>4)
            assert r == g == b
    assert len(seen) == width*height == len(data)


def main():
    # Exercise every luminance/alpha combination across multiple tile rows.
    im = Image.new('RGBA',(32,16))
    im.putdata([(i%16*17,)*3+(i//16%16*17,) for i in range(512)])
    check_pixels(encode_la4(im),*im.size,im)
    print('PASS: all LA4 channel combinations and tile addresses')
    try:
        encode_la4(Image.new('RGBA',(8,8),(255,0,0,255)))
    except AssertionError:
        print('PASS: rejects color artwork in grayscale encoder')
    else:
        raise AssertionError('Accepted colored logo')
    art = Path('build/home-menu/art')
    r = Reader((art/'banner.cgfx').read_bytes())
    logo = next(t for t in r.dictionary(36).values() if r.u32(t+52)==9)
    pixel = r.ptr(logo+56)
    start,size = r.ptr(pixel+12),r.u32(pixel+8)
    check_pixels(r.data[start:start+size],r.u32(logo+28),r.u32(logo+24),
                 Image.open(art/'logo.png'))
    print('PASS: all 131072 serialized logo texels match source quantization')


if __name__ == '__main__':
    main()
