"""Encode the monochrome banner logo at full resolution within HOME's budget."""
from PIL import Image


def encode_la4(image):
    """PICA LA4: high nibble luminance, low nibble alpha, 8x8 Morton tiles.

    Four times the texels of the previous RGBA4 logo cost only twice the
    bytes. Both formats have the same 4-bit precision for this grayscale art.
    """
    image = image.convert('RGBA')
    w, h = image.size
    assert w >= 8 and h >= 8 and not (w & (w-1) or h & (h-1))
    pixels = image.load()
    output = bytearray(w*h)
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, h-1-y]
            assert r == g == b, 'LA4 logo must be grayscale'
            morton = sum(((x >> i) & 1) << (2*i) |
                         ((y >> i) & 1) << (2*i+1) for i in range(3))
            offset = ((y//8)*(w//8)+x//8)*64+morton
            output[offset] = (r & 0xf0) | (a >> 4)
    return bytes(output)


def replace_logo_texture(banner, source, directory):
    """Bypass the converter's implicit 256px cap for the logo only."""
    material = next(m for m in source.model.materials if m.name == 'Logo')
    texture = source.model.textures[material.pbrMetallicRoughness.baseColorTexture.index]
    image = source.model.images[texture.source]
    target = banner.data.textures[image.name or image.uri]
    im = Image.open(directory / image.uri).convert('RGBA')
    assert im.size == (512,256)
    from cgfx.txob import TextureFormat
    target.hw_format = TextureFormat.LA4
    target.width = target.pixel_based_image.width = im.width
    target.height = target.pixel_based_image.height = im.height
    target.pixel_based_image.data = encode_la4(im)
    target.mipmap_level_count = 1
