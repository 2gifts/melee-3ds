"""Check both serialized RGBA4 textures and the conservative logo resolution."""
from pathlib import Path
import argparse
from PIL import Image
from home_banner_texture import prepare_logo
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
            sample = int.from_bytes(data[2*offset:2*offset+2],'little')
            r,g,b,a = pixels[x,y]
            assert (sample>>12,(sample>>8)&15,(sample>>4)&15,sample&15) == (r>>4,g>>4,b>>4,a>>4)
    assert len(seen) == width*height == len(data)//2


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--art',type=Path,default=Path('build/home-menu/art'))
    art = ap.parse_args().art
    r = Reader((art/'banner.cgfx').read_bytes())
    pixels = 0
    for name,texture in r.dictionary(36).items():
        assert r.u32(texture+52)==4, 'Only physical-tested RGBA4 is allowed'
        height,width = r.read('II',texture+24)
        assert max(width,height)<=256
        image = Image.open(art/name).convert('RGBA')
        assert image.size == (width,height)
        if height==128:
            expected = prepare_logo(Image.open(art/'logo.png'))
            assert image.tobytes()==expected.tobytes()
        pixel = r.ptr(texture+56)
        start,size = r.ptr(pixel+12),r.u32(pixel+8)
        check_pixels(r.data[start:start+size],width,height,image)
        pixels += width*height
    assert pixels==98304
    print('PASS: all 98304 serialized atlas/logo texels match source quantization')
    print('PASS: package 5 texture dimensions, RGBA4 format and 192 KiB byte budget')


if __name__ == '__main__':
    main()
