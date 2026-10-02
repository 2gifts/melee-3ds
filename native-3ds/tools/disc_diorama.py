"""The 3D HOME Menu diorama, made from the user's disc files alone.

Two Foxes on Final Destination with the title logo, like the banner first
authored from emulator captures (capture_banner_scene.py and
make_home_menu_art.py). Here nothing is captured: the models, animations,
stage and textures are read from the user's extracted disc, posed with the
game's own math (hsd_model.py), and written as the same rigid four-draw
scene. This file holds only the scene's layout: costumes, animation frames,
which model parts are shown, the camera angle and the stage colours.

The poses equal the captured ones to float rounding. The captured stage came
from optional third-party stage visuals; this one is the original Final
Destination, simplified to fit the HOME Menu budget of the console-tested
banner (package 5).
"""
import hashlib
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

from assets import ROOT
import hsd_model as hsd

FILES = ROOT/'assets/GALE01/files'
OUT = ROOT/'build/home-menu/diorama'

# Rotation of the gameplay camera in the original captures (world to view).
# Its position does not matter: the scene is re-centred on the stage.
CAMERA = ((0.9999466538429260, 0.0, -0.010333061218261719),
          (-0.0013797760009765625, 0.9910436868667603, -0.13353058695793152),
          (0.010240554809570312, 0.13353771716356277, 0.9909907579421997))

# Fox 1 taunts ("Appeal", frame 67) in the default costume, facing right;
# Fox 2 stands idle ("Wait1", frame 29) in the orange costume, facing left.
FIGHTERS = (dict(costume='PlFxNr.dat', action=239, frame=67, facing=1),
            dict(costume='PlFxOr.dat', action=2, frame=29, facing=-1))
# The model parts drawn in the captures (DObjs of the root joint).
FOX_PARTS = [i for i in range(36, 68) if i != 40]

# Final Destination's main model: (joint, DObj, colour, simplification).
# The banner's flat purple-metal finish, as in the console-tested banner.
STAGE = ((3, 3, (.40, .30, .97, 1), 0),   # top panels, textured (the first part)
         (3, 4, (.40, .30, .97, 1), 0),
         (3, 5, (.32, .24, .78, 1), 0),   # panel borders
         (3, 17, (.10, .07, .20, 1), 5),  # rim
         (4, 0, (.22, .16, .34, 1), 5))   # underside
STAGE_GOBJ = 3
# Beneath the panels, whose gaps carry animated effects in the game: a
# plain base across the platform's outline, as in the captured banner.
BASE = (.075, .025, .16, 1)


def apply(m, p):
    """m (3x4) times points p (N,3), in float64 without BLAS."""
    m = np.asarray(m, dtype=float)
    p = np.asarray(p, dtype=float)
    return np.stack([p[:, 0]*m[r, 0]+p[:, 1]*m[r, 1]+p[:, 2]*m[r, 2]+m[r, 3] for r in range(3)], 1)


def rotate(m, v):
    m = np.asarray(m, dtype=float)
    v = np.asarray(v, dtype=float)
    return np.stack([v[:, 0]*m[r, 0]+v[:, 1]*m[r, 1]+v[:, 2]*m[r, 2] for r in range(3)], 1)


def cofactor(m):
    """Inverse-transpose of a 3x3 up to a positive scale: transforms normals.
    (Mirrored joints have a negative determinant; keep normals outward.)"""
    a = [[float(m[r][c]) for c in range(3)] for r in range(3)]
    c = [[a[(r+1) % 3][(k+1) % 3]*a[(r+2) % 3][(k+2) % 3]-a[(r+1) % 3][(k+2) % 3]*a[(r+2) % 3][(k+1) % 3]
          for k in range(3)] for r in range(3)]
    det = a[0][0]*c[0][0]+a[0][1]*c[0][1]+a[0][2]*c[0][2]
    return [[v if det > 0 else -v for v in row] for row in c]


def unit(v):
    v = np.asarray(v, dtype=float)
    length = np.sqrt(v[:, 0]*v[:, 0]+v[:, 1]*v[:, 1]+v[:, 2]*v[:, 2])
    return v/np.maximum(length, 1e-12)[:, None]


def cross(a, b):
    return np.stack([a[:, 1]*b[:, 2]-a[:, 2]*b[:, 1], a[:, 2]*b[:, 0]-a[:, 0]*b[:, 2],
                     a[:, 0]*b[:, 1]-a[:, 1]*b[:, 0]], 1)


def fighter(spec, fighter_dat, animations):
    dat = hsd.Dat.load(FILES/spec['costume'])
    root = next(v for k, v in dat.roots.items() if k.endswith('_joint') and 'matanim' not in k)
    joints = hsd.joints(dat, root)
    tree = hsd.fighter_animation(fighter_dat, animations, spec['action'])
    attributes = fighter_dat.u32(next(iter(fighter_dat.roots.values())))
    scale = fighter_dat.f32(attributes+0x8C)  # ftCo_DatAttrs.model_scaling
    # Fighter root: model scale and facing (fighter.c: M_PI_2 * facing_dir).
    facing = np.float32(math.pi/2*spec['facing'])
    world = hsd.pose(joints, tree, spec['frame'], root=((scale,)*3, (0, facing, 0), (0, 0, 0)))
    parts = []
    for index in FOX_PARTS:
        dobj = joints[0].dobjs[index]
        assert len(dobj.pobjs) == 1 and len(dobj.mobj.textures) == 1
        pobj = dobj.pobjs[0]
        envelopes = hsd.envelope_matrices(joints, pobj, world, joints[0])
        corners = [v for t in hsd.triangles(hsd.primitives(dat, pobj)) for v in t]
        pos = np.array([v[hsd.GX_VA_POS] for v in corners], dtype=np.float32)
        nrm = np.array([v[hsd.GX_VA_NRM] for v in corners], dtype=np.float32)
        uv = np.array([v[hsd.GX_VA_TEX0] for v in corners], dtype=np.float32)
        which = np.array([v[hsd.GX_VA_PNMTXIDX]//3 for v in corners])
        view = np.empty((len(corners), 3))
        normal = np.empty((len(corners), 3))
        for e in np.unique(which):
            mask = which == e
            view[mask] = rotate(CAMERA, apply(envelopes[e], pos[mask]))
            normal[mask] = rotate(CAMERA, rotate(cofactor(envelopes[e][:, :3]), nrm[mask]))
        n = len(corners)
        # GX front faces are clockwise; glTF and CGFX's are counter-clockwise.
        # One two-sided part stays single-sided, as in the console-tested
        # banner: the only package that doubled it froze HOME (package 7).
        triangles = np.arange(n).reshape(-1, 3)[:, [0, 2, 1]]
        parts.append(dict(positions=view, normals=unit(normal), uv=uv, colors=np.ones((n, 4)),
                          indices=triangles.reshape(-1), image=hsd.tobj_image(dat, dobj.mobj.textures[0])))
    return parts


def weld(pos, uv):
    key = np.concatenate([np.round(pos, 4), uv], 1)
    unique, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    return pos[first], uv[first], inverse.reshape(-1)


def simplify(pos, triangles, cell):
    """Vertex clustering: merge the vertices in each CELL-sized cube."""
    key = np.floor(pos/cell).astype(np.int64)
    _, inverse = np.unique(key, axis=0, return_inverse=True)
    inverse = inverse.reshape(-1)
    count = np.bincount(inverse)
    merged = np.zeros((len(count), 3))
    for k in range(3):
        merged[:, k] = np.bincount(inverse, weights=pos[:, k])/count
    t = inverse[triangles]
    keep = (t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])
    t = t[keep]
    _, first = np.unique(np.sort(t, 1), axis=0, return_index=True)
    return merged, t[np.sort(first)]


def convex_hull(points):
    """Counter-clockwise hull of 2D points (monotone chain)."""
    points = sorted(set(points))
    def turn(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    hull = []
    for sequence in (points, points[::-1]):
        chain = []
        for q in sequence:
            while len(chain) >= 2 and turn(chain[-2], chain[-1], q) <= 0:
                chain.pop()
            chain.append(q)
        hull += chain[:-1]
    return hull


def smooth_normals(pos, triangles):
    """Area-weighted vertex normals of clockwise (GX) triangles."""
    a, b, c = pos[triangles[:, 0]], pos[triangles[:, 1]], pos[triangles[:, 2]]
    face = cross(c-a, b-a)
    normal = np.zeros_like(pos)
    for k in range(3):
        for corner in range(3):
            normal[:, k] += np.bincount(triangles[:, corner], weights=face[:, k], minlength=len(pos))
    return unit(normal)


def soften(image):
    """The panels' metal noise at half contrast around a bright mean, so
    their colour carries the banner's blue-purple instead of grey."""
    grey = image.convert('L')
    histogram = grey.histogram()
    mean = sum(i*n for i, n in enumerate(histogram))/sum(histogram)
    lut = [max(0, min(255, round(205+(i-mean)*.5))) for i in range(256)]
    return grey.point(lut).convert('RGBA')


def stage():
    dat = hsd.Dat.load(FILES/'GrNLa.dat')
    gobjs = dat.u32(dat.roots['map_head']+8)
    joints = hsd.joints(dat, dat.u32(gobjs+STAGE_GOBJ*0x34))
    world = hsd.pose(joints)
    parts = []
    for joint, index, tint, cell in STAGE:
        dobj = joints[joint].dobjs[index]
        assert len(dobj.pobjs) == 1
        corners = [v for t in hsd.triangles(hsd.primitives(dat, dobj.pobjs[0])) for v in t]
        local = np.array([v[hsd.GX_VA_POS] for v in corners], dtype=np.float32).astype(float)
        # Simplified parts are flat-coloured; their texture cannot follow.
        textured = bool(dobj.mobj and dobj.mobj.textures) and not cell
        uv = np.array([v[hsd.GX_VA_TEX0] if textured else (0, 0) for v in corners], dtype=float)
        local, uv, triangles = weld(local, uv)
        triangles = triangles.reshape(-1, 3)
        pos = apply(world[joint], local)
        if cell:
            pos, triangles = simplify(pos, triangles, cell)
            uv = np.zeros((len(pos), 2))
        parts.append(dict(local=local,
                          positions=rotate(CAMERA, pos), normals=rotate(CAMERA, smooth_normals(pos, triangles)),
                          uv=uv, indices=triangles.reshape(-1), tint=tint,
                          image=soften(hsd.tobj_image(dat, dobj.mobj.textures[0])) if textured else None))
    # The top surface lies in its joint's y = 0 plane.
    top = STAGE[0][0]
    up = rotate(CAMERA, unit(rotate(cofactor(np.asarray(world[top])[:, :3]), [[0, 1, 0]])))[0]
    surface = parts[0]['positions']
    plane = float(np.median(surface[:, 0]*up[0]+surface[:, 1]*up[1]+surface[:, 2]*up[2]))
    assert np.abs(surface[:, 0]*up[0]+surface[:, 1]*up[1]+surface[:, 2]*up[2]-plane).max() < 1e-3
    # The base sits just below the panels, so the two cannot z-fight.
    outline = convex_hull([tuple(q) for part in parts[:3] for q in np.round(part['local'][:, [0, 2]], 4)])
    base = np.array([(x, -0.05, z) for x, z in outline])
    base = rotate(CAMERA, apply(world[top], base))
    fan = np.array([(0, k, k+1) for k in range(1, len(outline)-1)])
    parts.append(dict(positions=base, normals=np.tile(up, (len(base), 1)), uv=np.zeros((len(base), 2)),
                      indices=fan.reshape(-1), tint=BASE, image=None))
    for part in parts:
        part.pop('local', None)
    every = np.concatenate([p['positions'] for p in parts])
    center = (every.min(0)+every.max(0))/2
    return parts, center, up, plane


def logo():
    """Title-screen letters with a dark outline (as make_home_menu_art.logo_and_icon)."""
    from banner_assets import Archive, texture
    title = Archive(FILES/'GmTitle.usd')
    top = texture(title, 0x2724).convert('RGBA')
    lower = texture(title, 0x28b0).convert('RGBA').resize((300, 61), Image.Resampling.LANCZOS)
    image = Image.new('RGBA', (512, 256))
    image.alpha_composite(top, (40, 18))
    image.alpha_composite(lower, (106, 150))
    outline = image.getchannel('A').filter(ImageFilter.MaxFilter(5))
    shadow = Image.new('RGBA', image.size, (17, 17, 17, 255))
    shadow.putalpha(outline)
    shadow.alpha_composite(image)
    return shadow


def make_scene(out=OUT):
    """Write scene.gltf, its buffer and textures to OUT; return the art report."""
    import make_home_menu_art as art
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    fighter_dat = hsd.Dat.load(FILES/'PlFx.dat')
    animations = (FILES/'PlFxAJ.dat').read_bytes()
    fighters = [fighter(spec, fighter_dat, animations) for spec in FIGHTERS]
    parts, center, up, plane = stage()
    image = logo()
    image.save(out/'logo.png')
    return art.assemble_scene(parts, fighters, center, up, plane, image, out=out, atlas_height=256)


# The console-tested banner (package 5). Every later package that was even
# slightly larger froze HOME on selection, so stay within its sizes.
PACKAGE5 = dict(cgfx_bytes=403544, cbmd_bytes=499896, sound_frames=93422)
# Disc-made models confirmed on a physical New 3DS (both titles, 2026-10-02).
# A different hash is still checked for structure and size; it is reported
# as not console-tested.
CONSOLE_TESTED = {'7dbbb826d9b6e72601b98b0aec5d22933a234f60c0c7c393ba0d9445a8e33dab'}


def verify_diorama(packed, cgfx):
    """Structure and HOME Menu budget of a disc-made diorama banner."""
    from verify_home_banner import verify_banner
    from verify_cia import u32
    result = verify_banner(cgfx, require_outward=True)
    assert len(cgfx) <= PACKAGE5['cgfx_bytes'], f'Diorama exceeds the tested model size: {len(cgfx)}'
    assert len(packed) <= PACKAGE5['cbmd_bytes'], f'Banner exceeds the tested size: {len(packed)}'
    assert u32(packed, 0x84) % 16 == 0
    digest = hashlib.sha256(cgfx).hexdigest()
    result.update(made_from_disc=True, cgfx_sha256=digest, console_tested=digest in CONSOLE_TESTED,
                  budget=dict(cgfx_bytes=[len(cgfx), PACKAGE5['cgfx_bytes']],
                              cbmd_bytes=[len(packed), PACKAGE5['cbmd_bytes']]))
    return result


def make_art(profile, out=None):
    """banner.cgfx, banner.bin, announcer.wav and icon.png for package_cia.py."""
    import json
    import shutil
    import subprocess
    from convert_home_menu_banner import convert
    from launcher_icon import disc_icon
    from simple_banner import BANNERTOOL, banner_sound
    from verify_cia import lz11, u32
    out = Path(out or ROOT/f'build/home-menu/diorama-{profile}')
    out.mkdir(parents=True, exist_ok=True)
    scene = make_scene()
    convert(OUT, require_outward=True)
    shutil.copyfile(OUT/'banner.cgfx', out/'banner.cgfx')
    banner_sound(out/'announcer.wav', frames=PACKAGE5['sound_frames'])
    disc_icon(profile).save(out/'icon.png')
    subprocess.run([str(BANNERTOOL), 'makebanner', '-ci', str(out/'banner.cgfx'), '-a', str(out/'announcer.wav'),
                    '-o', str(out/'banner.bin')], check=True, capture_output=True)
    packed = (out/'banner.bin').read_bytes()
    cgfx = (out/'banner.cgfx').read_bytes()
    assert packed[:4] == b'CBMD' and lz11(packed[u32(packed, 8):]) == cgfx
    report = dict(scene=scene, banner=verify_diorama(packed, cgfx))
    (out/'diorama-report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return out


if __name__ == '__main__':
    import json
    import sys
    if sys.argv[1:]:
        for name in sys.argv[1:]:
            print(make_art(name))
    else:
        print(json.dumps(make_scene(), indent=2))
