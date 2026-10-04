"""Read HSD models, textures and joint animations from the user's disc files.

Only what the HOME Menu diorama needs: the joint tree, display objects,
materials, textures, polygon display lists and figatree joint animation.
Structure layouts follow the decompiled sysdolphin/baselib sources.
"""
import struct
from pathlib import Path

GX_VA_PNMTXIDX, GX_VA_POS, GX_VA_NRM, GX_VA_CLR0, GX_VA_TEX0 = 0, 9, 10, 11, 13
GX_DIRECT, GX_INDEX8, GX_INDEX16 = 1, 2, 3
POBJ_TYPE_MASK, POBJ_SKIN, POBJ_SHAPEANIM, POBJ_ENVELOPE = 0x3000, 0, 0x1000, 0x2000
POBJ_CULLFRONT, POBJ_CULLBACK = 0x4000, 0x8000


class Dat:
    """One HSD archive: a data block addressed by offset, plus named roots."""

    def __init__(self, raw):
        size, data, reloc, public, extern = struct.unpack_from('>5I', raw)
        assert size == len(raw), 'HSD archive size mismatch'
        self.b = raw[32:32+data]
        names = 32+data+4*reloc+8*public+8*extern
        self.roots = {}
        for i in range(public):
            offset, name = struct.unpack_from('>II', raw, 32+data+4*reloc+8*i)
            end = raw.index(b'\0', names+name)
            self.roots[raw[names+name:end].decode('ascii')] = offset

    @classmethod
    def load(cls, path):
        return cls(Path(path).read_bytes())

    def u8(self, o): return self.b[o]
    def u16(self, o): return struct.unpack_from('>H', self.b, o)[0]
    def u32(self, o): return struct.unpack_from('>I', self.b, o)[0]
    def f32(self, o): return struct.unpack_from('>f', self.b, o)[0]
    def floats(self, o, n): return struct.unpack_from(f'>{n}f', self.b, o)


class Joint:
    def __init__(self, dat, offset, parent, index):
        self.offset, self.parent, self.index = offset, parent, index
        self.flags = dat.u32(offset+4)
        self.rotate = dat.floats(offset+0x14, 3)
        self.scale = dat.floats(offset+0x20, 3)
        self.translate = dat.floats(offset+0x2C, 3)
        inverse = dat.u32(offset+0x38)
        self.inverse_bind = dat.floats(inverse, 12) if inverse else None
        self.children = []
        # Bit 0x4000 (JOBJ_SPLINE) and 0x20 (particle) reuse the DObj slot.
        self.dobjs = []
        if not self.flags & 0x4020:
            o = dat.u32(offset+0x10)
            while o:
                self.dobjs.append(DObj(dat, o))
                o = dat.u32(o+4)


class DObj:
    def __init__(self, dat, offset):
        self.offset = offset
        self.mobj = MObj(dat, dat.u32(offset+8)) if dat.u32(offset+8) else None
        self.pobjs = []
        o = dat.u32(offset+12)
        while o:
            self.pobjs.append(PObj(dat, o))
            o = dat.u32(o+4)


class MObj:
    def __init__(self, dat, offset):
        self.offset = offset
        self.rendermode = dat.u32(offset+4)
        self.textures = []
        o = dat.u32(offset+8)
        while o:
            self.textures.append(TObj(dat, o))
            o = dat.u32(o+4)
        m = dat.u32(offset+12)
        self.ambient, self.diffuse, self.specular = (tuple(dat.b[m+i:m+i+4]) for i in (0, 4, 8)) if m else (None,)*3
        self.alpha = dat.f32(m+12) if m else 1.0


class TObj:
    def __init__(self, dat, offset):
        self.offset = offset
        self.id, self.src = dat.u32(offset+8), dat.u32(offset+12)
        self.rotate, self.scale, self.translate = (dat.floats(offset+o, 3) for o in (0x10, 0x1C, 0x28))
        self.wrap = dat.u32(offset+0x34), dat.u32(offset+0x38)
        self.repeat = dat.u8(offset+0x3C), dat.u8(offset+0x3D)
        self.flags = dat.u32(offset+0x40)
        image = dat.u32(offset+0x4C)
        self.image = (dat.u32(image), dat.u16(image+4), dat.u16(image+6), dat.u32(image+8)) if image else None
        tlut = dat.u32(offset+0x50)
        self.tlut = (dat.u32(tlut), dat.u32(tlut+4), dat.u16(tlut+12)) if tlut else None


class PObj:
    def __init__(self, dat, offset):
        self.offset = offset
        self.flags, blocks = dat.u16(offset+12), dat.u16(offset+14)
        self.display = dat.b[dat.u32(offset+16):dat.u32(offset+16)+32*blocks]
        self.attributes = []
        o = dat.u32(offset+8)
        while dat.u32(o) != 0xFF:
            attr, kind, count, ctype = struct.unpack_from('>4I', dat.b, o)
            frac, stride, data = dat.u8(o+16), dat.u16(o+18), dat.u32(o+20)
            self.attributes.append((attr, kind, count, ctype, frac, stride, data))
            o += 24
        link = dat.u32(offset+20)
        self.type = self.flags & POBJ_TYPE_MASK
        self.joint = link if self.type == POBJ_SKIN and link else None
        self.envelopes = []
        if self.type == POBJ_ENVELOPE:
            while dat.u32(link):
                e, weights = dat.u32(link), []
                while dat.u32(e):
                    weights.append((dat.u32(e), dat.f32(e+4)))
                    e += 8
                self.envelopes.append(weights)
                link += 4


def component_size(ctype):
    return (1, 1, 2, 2, 4)[ctype]


def direct_size(attr, count, ctype):
    if attr < GX_VA_POS:
        return 1
    if attr in (GX_VA_CLR0, GX_VA_CLR0+1):
        return (2, 3, 4, 2, 3, 4)[ctype]
    n = 3 if attr == GX_VA_NRM else (2 if count == 0 else 3) if attr == GX_VA_POS else (1 if count == 0 else 2)
    return n*component_size(ctype)


def scalar(dat, o, ctype, frac):
    """The engine's float32 conversion (port/engine/gx.c scalar)."""
    import numpy as np
    raw = (dat.b[o], struct.unpack_from('>b', dat.b, o)[0], struct.unpack_from('>H', dat.b, o)[0],
           struct.unpack_from('>h', dat.b, o)[0]) if ctype < 4 else None
    if ctype == 4:
        return np.float32(dat.f32(o))
    return np.float32(raw[ctype])/np.float32(1 << frac)


def components(dat, attr, count, ctype, frac, o):
    n = 3 if attr == GX_VA_NRM else (2 if count == 0 else 3) if attr == GX_VA_POS else (1 if count == 0 else 2)
    return [scalar(dat, o+i*component_size(ctype), ctype, frac) for i in range(n)]


def color(dat, o, ctype):
    b = dat.b
    if ctype == 0:
        x = struct.unpack_from('>H', b, o)[0]
        return (((x >> 11) & 31)*255//31, ((x >> 5) & 63)*255//63, (x & 31)*255//31, 255)
    if ctype in (1, 2):
        return (b[o], b[o+1], b[o+2], 255)
    if ctype == 3:
        x = struct.unpack_from('>H', b, o)[0]
        return tuple(((x >> s) & 15)*17 for s in (12, 8, 4, 0))
    if ctype == 4:
        x = (b[o] << 16) | (b[o+1] << 8) | b[o+2]
        return tuple(((x >> s) & 63)*255//63 for s in (18, 12, 6, 0))
    return tuple(b[o:o+4])


def primitives(dat, pobj):
    """[(GX primitive opcode, [vertex dict])] in display-list order."""
    sizes = []
    for attr, kind, count, ctype, frac, stride, data in pobj.attributes:
        sizes.append(direct_size(attr, count, ctype) if kind == GX_DIRECT else 1 if kind == GX_INDEX8 else 2)
    out, p, d = [], 0, pobj.display
    while p < len(d) and d[p]:
        op = d[p]
        assert op & 0xF8 in (0x80, 0x90, 0x98, 0xA0), f'Unsupported GX primitive {op:#x}'
        n = struct.unpack_from('>H', d, p+1)[0]
        p += 3
        vertices = []
        for _ in range(n):
            v = {}
            for (attr, kind, count, ctype, frac, stride, data), size in zip(pobj.attributes, sizes):
                if kind == GX_DIRECT and attr < GX_VA_POS:
                    v[attr] = d[p]
                elif kind == GX_DIRECT:
                    inline = Dat.__new__(Dat)
                    inline.b = d[p:p+size]
                    v[attr] = color(inline, 0, ctype) if attr in (GX_VA_CLR0, GX_VA_CLR0+1) else \
                        components(inline, attr, count, ctype, frac, 0)
                else:
                    index = d[p] if kind == GX_INDEX8 else struct.unpack_from('>H', d, p)[0]
                    v[(attr, 'index')] = index
                    o = data+index*stride
                    if attr in (GX_VA_CLR0, GX_VA_CLR0+1):
                        v[attr] = color(dat, o, ctype)
                    else:
                        v[attr] = components(dat, attr, count, ctype, frac, o)
                p += size
            vertices.append(v)
        out.append((op & 0xF8, vertices))
    return out


def triangles(prims):
    """The engine's triangle order for each primitive (port/engine/gx.c vertex)."""
    out = []
    for op, verts in prims:
        first = 0
        for n in range(len(verts)):
            if op == 0x90 and n % 3 == 2:
                out.append((verts[n-2], verts[n-1], verts[n]))
            elif op == 0x98 and n >= 2:
                out.append((verts[n-1], verts[n-2], verts[n]) if n & 1 else (verts[n-2], verts[n-1], verts[n]))
            elif op == 0xA0 and n >= 2:
                out.append((verts[0], verts[n-1], verts[n]))
            elif op == 0x80:
                if n % 4 == 0:
                    first = n
                elif n % 4 in (2, 3):
                    out.append((verts[first], verts[n-1], verts[n]))
    return out


def joints(dat, root):
    """Depth-first joint list, in HSD's child-before-sibling order."""
    result = []

    def walk(o, parent):
        while o:
            j = Joint(dat, o, parent, len(result))
            result.append(j)
            if parent:
                parent.children.append(j)
            walk(dat.u32(o+8), j)
            o = dat.u32(o+12)
    walk(root, None)
    return result


class Track:
    def __init__(self, data, start, kind, frac_value, frac_slope):
        self.data, self.start, self.kind = data, start, kind
        self.frac_value, self.frac_slope = frac_value, frac_slope


class FigaTree:
    """A fighter joint animation: tracks per joint, in joint order."""

    def __init__(self, raw):
        dat = Dat(raw)
        root = next(iter(dat.roots.values()))
        self.type, self.flags, self.frames = dat.u32(root), dat.u32(root+4), dat.f32(root+8)
        nodes, tracks = dat.u32(root+12), dat.u32(root+16)
        self.joints, n = [], 0
        while dat.b[nodes+len(self.joints)] != 0xFF:
            count = dat.b[nodes+len(self.joints)]
            entry = []
            for k in range(count):
                o = tracks+12*(n+k)
                length, start = dat.u16(o), dat.u16(o+2)
                ad = dat.u32(o+8)
                entry.append(Track(dat.b[ad:ad+length], start, dat.u8(o+4), dat.u8(o+5), dat.u8(o+6)))
            self.joints.append(entry)
            n += count


def fighter_animation(fighter_dat, animations, index):
    """The figatree of a fighter's action table entry (PlXx.dat, PlXxAJ.dat)."""
    root = next(iter(fighter_dat.roots.values()))
    entry = fighter_dat.u32(root+0xC)+0x18*index
    offset, size = fighter_dat.u32(entry+4), fighter_dat.u32(entry+8)
    return FigaTree(animations[offset:offset+size])


# Float32 arithmetic in the engine's operation order. numpy float32 scalars
# round every operation like the ARM11 VFP (which has no fused multiply-add).
def _f32():
    import numpy as np
    return np.float32


def parse_float(data, pos, frac):
    F = _f32()
    if frac == 0:
        return F(struct.unpack_from('<f', data, pos)[0]), pos+4
    kind, denom = frac & 0xE0, F(1 << (frac & 0x1F))
    if kind == 0x60:
        return F(struct.unpack_from('<b', data, pos)[0])/denom, pos+1
    if kind == 0x80:
        return F(data[pos])/denom, pos+1
    if kind == 0x20:
        return F(struct.unpack_from('<h', data, pos)[0])/denom, pos+2
    if kind == 0x40:
        return F(struct.unpack_from('<H', data, pos)[0])/denom, pos+2
    return F(0), pos


def hermite(fterm, time, p0, p1, d0, d1):
    """splGetHelmite (sysdolphin/baselib/spline.c)."""
    F = _f32()
    t2 = time*time
    tt = fterm*fterm
    t2_t = t2*fterm
    t3_t2 = tt*(t2*time)
    a = F(2)*t3_t2*fterm
    b = F(3)*t2*tt
    return (d1*(t3_t2-t2_t)) + ((d0*(time+((t3_t2-t2_t)-t2_t))) + ((p0*(F(1)+(a-b))) + (p1*(-a+b))))


class FObj:
    """HSD_FObj keyframe interpreter (sysdolphin/baselib/fobj.c)."""

    def __init__(self, track, start=0.0):
        F = _f32()
        self.t, self.ad = track, 0
        self.time = F(F(track.start)+F(start))
        self.op = self.op_intrp = self.flags = self.nb_pack = self.fterm = 0
        self.p0 = self.p1 = self.d0 = self.d1 = F(0)
        self.state, self.value = 1, None

    def _wait(self):
        wait = shift = 0
        while True:
            d = self.t.data[self.ad]; self.ad += 1
            wait |= (d & 0x7F) << shift; shift += 7
            if not d & 0x80:
                return wait

    def _launch_key(self):
        if self.flags & 0x40:
            self.op_intrp = self.op
            self.flags = (self.flags & ~0x40) | 0x80
            self.p0 = self.p1

    def _load_data(self):
        F, data = _f32(), self.t.data
        if self.ad >= len(data):
            return 6
        self.op_intrp = self.op
        if self.nb_pack == 0:
            d = data[self.ad]; self.ad += 1
            self.op, self.nb_pack, shift = d & 0xF, ((d >> 4) & 7)+1, 3
            while d & 0x80:
                d = data[self.ad]; self.ad += 1
                self.nb_pack += (d & 0x7F) << shift; shift += 7
        self.nb_pack -= 1
        st, op = self.state, self.op
        if op in (1, 2):
            self.p0 = self.p1
            self.p1, self.ad = parse_float(data, self.ad, self.t.frac_value)
            if self.op_intrp != 5:
                self.d0, self.d1 = self.d1, F(0)
        elif op == 3:
            self.p0, self.d0 = self.p1, self.d1
            self.p1, self.ad = parse_float(data, self.ad, self.t.frac_value)
            self.d1 = F(0)
        elif op == 4:
            self.p0 = self.p1
            self.p1, self.ad = parse_float(data, self.ad, self.t.frac_value)
            self.d0 = self.d1
            self.d1, self.ad = parse_float(data, self.ad, self.t.frac_slope)
        elif op == 5:
            self.d0 = self.d1
            self.d1, self.ad = parse_float(data, self.ad, self.t.frac_slope)
            return st
        elif op == 6:
            self._launch_key()
            self.p1, self.ad = parse_float(data, self.ad, self.t.frac_value)
            self.flags |= 0x40
        else:
            return 0
        self.state = 3 if st == 1 else 4
        return self.state

    def _update(self):
        F = _f32()
        op = self.op_intrp
        if op == 6:
            if not self.flags & 0x80:
                return
            value = self.p0; self.flags &= ~0x80
        elif op == 1:
            value = self.p1 if self.time >= F(self.fterm) else self.p0
        elif op == 2:
            if self.flags & 0x20:
                self.flags &= ~0x20
                if self.fterm:
                    self.d0 = (self.p1-self.p0)/F(self.fterm)
                else:
                    self.d0, self.p0 = F(0), self.p1
            value = self.d0*self.time+self.p0
        elif op in (3, 4, 5):
            value = hermite(F(1.0/self.fterm), self.time, self.p0, self.p1, self.d0, self.d1) \
                if self.fterm else self.p1
        else:
            raise AssertionError('Keyframe interpolation without a previous key')
        self.value = value

    def interpret(self, rate):
        F = _f32()
        fterm, state = F(0), self.state
        if not state:
            return
        self.time = F(self.time+F(rate))
        if self.time < 0:
            return
        while True:
            if state == 6:
                self.time = F(self.time+fterm)
                self._launch_key(); self._update()
                return
            if state in (1, 2):
                state = self._load_data()
            elif state == 3:
                if self.flags & 0x80:
                    self._update()
                if self.ad >= len(self.t.data):
                    state = 6
                else:
                    self.fterm = self._wait(); self.flags |= 0x20
                    state = self.state = 2
            elif state == 4:
                if F(self.fterm) <= self.time:
                    state = self.state = 3
                    fterm = F(self.fterm)
                    self.time = F(self.time-fterm)
                    continue
                self._update()
                self.state = 5
                return
            elif state == 5:
                state = self.state = 4
            else:
                return


def track_value(track, frame):
    """The value a track gives after ReqAnim(0) and `frame` steps at rate 1."""
    fobj = FObj(track)
    fobj.interpret(0.0)
    for _ in range(frame):
        fobj.interpret(1.0)
    return fobj.value


def mtx_srt(scale, rotate, translate, parent_scale):
    """HSD_MtxSRT (sysdolphin/baselib/mtx.c) with libm sinf/cosf."""
    import math
    import numpy as np
    F = np.float32
    def sincos(a):
        if a == 0:
            return F(a), F(1)
        return F(math.sin(float(a))), F(math.cos(float(a)))
    sx, cx = sincos(rotate[0]); sy, cy = sincos(rotate[1]); sz, cz = sincos(rotate[2])
    x2 = x1 = x = F(scale[0]); y2 = y1 = y = F(scale[1]); z2 = z1 = z = F(scale[2])
    if parent_scale is not None:
        px, py, pz = (F(v) for v in parent_scale)
        t1, t2, t3 = F(1.0/float(px)), F(1.0/float(py)), F(1.0/float(pz))
        y2 = y2*(py*t1); z2 = z2*(pz*t1)
        x1 = x1*(px*t2); z1 = z1*(pz*t2)
        x = x*(px*t3); y = y*(py*t3)
    m = np.zeros((3, 4), dtype=F)
    m[0, 0] = cz*(x2*cy); m[1, 0] = sz*(x1*cy); m[2, 0] = -x*sy
    m[0, 1] = y2*((cz*(sx*sy))-(cx*sz)); m[1, 1] = y1*((sz*(sx*sy))+(cx*cz)); m[2, 1] = cy*(y*sx)
    m[0, 2] = z2*((cz*(cx*sy))+(sx*sz)); m[1, 2] = z1*((sz*(cx*sy))-(sx*cz)); m[2, 2] = cy*(z*cx)
    m[:, 3] = [F(v) for v in translate]
    return m


def mtx_concat(a, b):
    """C_MTXConcat (Dolphin SDK portable C), the engine's PSMTXConcat."""
    import numpy as np
    m = np.zeros((3, 4), dtype=np.float32)
    for i in range(3):
        for j in range(3):
            m[i, j] = a[i, 2]*b[2, j] + ((a[i, 0]*b[0, j]) + (a[i, 1]*b[1, j]))
        m[i, 3] = a[i, 3] + (a[i, 2]*b[2, 3] + (a[i, 0]*b[0, 3] + (a[i, 1]*b[1, 3])))
    return m


def pose(joint_list, tree=None, frame=0, root=None, classical=None):
    """World matrices (float32 3x4) of a joint tree, HSD_JObjMakeMatrix order.

    tree/frame: a FigaTree and the number of rate-1 steps after it started.
    root: optional (scale, rotate, translate) replacing the first joint's SRT.
    """
    import numpy as np
    F = np.float32
    values = []
    for j in joint_list:
        values.append([list(map(F, j.scale)), list(map(F, j.rotate)), list(map(F, j.translate)), j.flags])
    if tree is not None:
        for j, tracks in zip(joint_list, tree.joints):
            if tracks and tree.type & 1:
                values[j.index][3] |= 8
            elif tracks:
                values[j.index][3] &= ~8
            for track in tracks:
                v = track_value(track, frame)
                if v is None:
                    continue
                kind = track.kind
                if 1 <= kind <= 3:
                    values[j.index][1][kind-1] = v
                elif 5 <= kind <= 7:
                    values[j.index][2][kind-5] = v
                elif 8 <= kind <= 10:
                    values[j.index][0][kind-8] = F(1e-3) if abs(v) < F(1e-3) else v
    if classical is not None:
        for index, flag in classical.items():
            values[index][3] = values[index][3] | 8 if flag else values[index][3] & ~8
    if root is not None:
        values[0][0], values[0][1], values[0][2] = ([F(v) for v in part] for part in root)
    world, scl = [], []
    for j in joint_list:
        scale, rotate, translate, flags = values[j.index]
        parent_scl = scl[j.parent.index] if j.parent else None
        if flags & 8:
            own = list(parent_scl) if parent_scl is not None else None
        else:
            own = [scale[k]*parent_scl[k] for k in range(3)] if parent_scl is not None else list(scale)
        assert not flags & 0x20000, 'Quaternion joints are not used by these models'
        m = mtx_srt(scale, rotate, translate, parent_scl)
        if j.parent:
            m = mtx_concat(world[j.parent.index], m)
        world.append(m)
        scl.append(own)
    return world


def envelope_matrices(dat_joints, pobj, world, owner):
    """SetupEnvelopeModelMtx without the view matrix: one 3x4 per envelope."""
    import numpy as np
    by_offset = {j.offset: j for j in dat_joints}
    assert owner.flags & 2, 'Only skeleton-root owners are supported'
    result = []
    for weights in pobj.envelopes:
        if len(weights) == 1 and weights[0][1] >= 1.0-1.1920929e-07:
            result.append(world[by_offset[weights[0][0]].index])
            continue
        m = np.zeros((3, 4), dtype=np.float32)
        for offset, weight in weights:
            j = by_offset[offset]
            inverse = np.array(j.inverse_bind, dtype=np.float32).reshape(3, 4)
            tmp = mtx_concat(world[j.index], inverse)
            m = tmp*np.float32(weight) + m
        result.append(m)
    return result


def _expand(bits, values):
    import numpy as np
    v = values.astype(np.uint32)
    if bits == 3:
        return (v << 5) | (v << 2) | (v >> 1)
    if bits == 4:
        return v*17
    if bits == 5:
        return (v << 3) | (v >> 2)
    return (v << 2) | (v >> 4)


def _rgb565(v):
    import numpy as np
    return np.stack([_expand(5, v >> 11), _expand(6, (v >> 5) & 63), _expand(5, v & 31), np.full(v.shape, 255, np.uint32)], -1)


def _rgb5a3(v):
    import numpy as np
    opaque = np.stack([_expand(5, (v >> 10) & 31), _expand(5, (v >> 5) & 31), _expand(5, v & 31),
                       np.full(v.shape, 255, np.uint32)], -1)
    clear = np.stack([((v >> 8) & 15)*17, ((v >> 4) & 15)*17, (v & 15)*17, _expand(3, (v >> 12) & 7)], -1)
    return np.where((v & 0x8000)[..., None] != 0, opaque, clear)


def decode_texture(data, width, height, fmt, palette=None, palette_format=0):
    """RGBA pixels exactly as the native renderer decodes them
    (port/3ds/renderer.c decode, port/3ds/texture_decode.h)."""
    import numpy as np
    from PIL import Image
    bw, bh, size = {0: (8, 8, 32), 1: (8, 4, 32), 2: (8, 4, 32), 3: (4, 4, 32), 4: (4, 4, 32), 5: (4, 4, 32),
                    6: (4, 4, 64), 8: (8, 8, 32), 9: (8, 4, 32), 10: (4, 4, 32), 14: (8, 8, 32)}[fmt]
    pitch, rows = (width+bw-1)//bw, (height+bh-1)//bh
    raw = np.frombuffer(bytes(data[:pitch*rows*size]), dtype=np.uint8).reshape(rows, pitch, size)
    if fmt == 14:
        sub = raw.reshape(rows, pitch, 2, 2, 8)  # 4x4 sub-blocks: [sy][sx]
        a = (sub[..., 0].astype(np.uint32) << 8) | sub[..., 1]
        b = (sub[..., 2].astype(np.uint32) << 8) | sub[..., 3]
        c0, c1 = _rgb565(a), _rgb565(b)
        mix53 = np.concatenate([(c0[..., :3]*5+c1[..., :3]*3) >> 3, c0[..., 3:]], -1)
        mix35 = np.concatenate([(c0[..., :3]*3+c1[..., :3]*5) >> 3, c0[..., 3:]], -1)
        avg = np.concatenate([(c0[..., :3]+c1[..., :3]) >> 1, c0[..., 3:]], -1)
        clear = avg.copy(); clear[..., 3] = 0
        gt = (a > b)[..., None]
        palette4 = np.stack([c0, c1, np.where(gt, mix53, avg), np.where(gt, mix35, clear)], -2)
        bits = sub[..., 4:8]  # one byte per row of 4 texels
        shifts = np.array([6, 4, 2, 0], dtype=np.uint8)
        index = (bits[..., :, None] >> shifts) & 3  # [.., row, col]
        pixel = np.take_along_axis(palette4[..., None, None, :, :],
                                   index[..., None, None].astype(np.intp), axis=-2)[..., 0, :]
        # pixel: rows, pitch, sy, sx, row, col, 4 -> image
        img = pixel.transpose(0, 2, 4, 1, 3, 5, 6).reshape(rows*8, pitch*8, 4)
    else:
        texels = bw*bh
        if fmt in (0, 8):
            nib = np.stack([raw >> 4, raw & 15], -1).reshape(rows, pitch, texels)
            v = nib.astype(np.uint32)
        elif fmt in (1, 2, 9):
            v = raw.astype(np.uint32)
        elif fmt == 6:
            ar, gb = raw[..., :32].reshape(rows, pitch, 16, 2), raw[..., 32:].reshape(rows, pitch, 16, 2)
            v = None
            rgba = np.stack([ar[..., 1], gb[..., 0], gb[..., 1], ar[..., 0]], -1).astype(np.uint32)
        else:
            v = (raw[..., 0::2].astype(np.uint32) << 8) | raw[..., 1::2]
        if fmt == 0:
            rgba = np.repeat((v*17)[..., None], 4, -1)
        elif fmt == 1:
            rgba = np.repeat(v[..., None], 4, -1)
        elif fmt == 2:
            i = (v & 15)*17
            rgba = np.stack([i, i, i, (v >> 4)*17], -1)
        elif fmt == 3:
            rgba = np.stack([v & 255, v & 255, v & 255, v >> 8], -1)
        elif fmt == 4:
            rgba = _rgb565(v)
        elif fmt == 5:
            rgba = _rgb5a3(v)
        elif fmt in (8, 9, 10):
            lut = np.frombuffer(bytes(palette), dtype='>u2').astype(np.uint32)
            colors = _rgb565(lut) if palette_format == 1 else _rgb5a3(lut) if palette_format == 2 else \
                np.stack([lut & 255, lut & 255, lut & 255, lut >> 8], -1)
            index = v & 0x3FFF if fmt == 10 else v
            valid = index < len(lut)
            rgba = np.where(valid[..., None], colors[np.minimum(index, len(lut)-1)], 255)
        img = rgba.reshape(rows, pitch, bh, bw, 4).transpose(0, 2, 1, 3, 4).reshape(rows*bh, pitch*bw, 4)
    return Image.fromarray(img[:height, :width].astype(np.uint8), 'RGBA')


def tobj_image(dat, tobj):
    offset, width, height, fmt = tobj.image
    palette = palette_format = None
    if tobj.tlut:
        lut, palette_format, count = tobj.tlut
        palette = dat.b[lut:lut+2*count]
    return decode_texture(dat.b[offset:], width, height, fmt, palette, palette_format)
