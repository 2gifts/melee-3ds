"""Decode the diagnostic's rotated BGR framebuffers to a PNG for visual QA."""
import argparse
from pathlib import Path
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("directory", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--engine", action="store_true", help="Decode the most recent original engine frame")
    args = ap.parse_args()
    screens = []
    for name, width, bpp in (("top",400,4), ("bottom",320,2)):
        ext = ".abgr" if bpp == 4 else ".rgb565"
        base = args.directory / (("engine-"+name) if args.engine else (name+"-screen"))
        if name=="top" and base.with_suffix('.bgr').exists():
            ext='.bgr';bpp=3
        data = base.with_suffix(ext).read_bytes()
        if len(data) != width*240*bpp:
            ap.error(f"Invalid {name} framebuffer byte count")
        if bpp == 2:
            import struct
            rgb = bytearray()
            for (v,) in struct.iter_unpack("<H", data):
                r,g,b = (v>>11)&31,(v>>5)&63,v&31
                rgb.extend(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)))
            data = bytes(rgb)
        if bpp == 4:
            im = Image.frombytes("RGBA", (240,width), data, "raw", "ABGR").convert("RGB")
        elif bpp == 3:
            im = Image.frombytes("RGB", (240,width), data, "raw", "BGR")
        else:
            im = Image.frombytes("RGB", (240,width), data)
        screens.append(im.transpose(Image.Transpose.ROTATE_90))
    combined = Image.new("RGB", (400,480), (15,15,15))
    combined.paste(screens[0], (0,0)); combined.paste(screens[1], (40,240))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    combined.save(args.output)
    print(args.output)


if __name__ == "__main__":
    main()
