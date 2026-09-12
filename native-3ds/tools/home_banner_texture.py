"""Keep the banner on the texture profile confirmed on physical New 3DS."""
from PIL import Image


def prepare_logo(image):
    # Package 6's 512x256 LA4 override rendered in Azahar but froze physical
    # HOME on selection. Keep package 5's dimensions and stock RGBA4 encoder.
    # These are our verified authoring constraints, not universal PICA limits.
    # Sharper letter faces/outline still improve clarity at this resolution.
    assert image.size == (512,256)
    return image.convert('RGBA').resize((256,128),Image.Resampling.LANCZOS)
