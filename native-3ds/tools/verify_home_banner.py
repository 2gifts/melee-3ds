"""Validate the serialized rigid CGFX subset used by our HOME Menu banner.

This deliberately reads bytes independently of pycgfx's authoring objects.
It cannot substitute for physical HOME Menu testing, but rejects soft skins,
broken relative pointers, missing animation bindings and invalid vertex data.
"""
import math
import struct


def outward_fraction(positions, normals, indices):
    """Area/normal-weighted winding agreement; tiny folded edges may differ."""
    positive = total = 0.0
    for i in range(0,len(indices),3):
        ids = indices[i:i+3]
        a,b,c = [positions[j] for j in ids]
        u = [b[k]-a[k] for k in range(3)]
        v = [c[k]-a[k] for k in range(3)]
        face = (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        dot = sum(face[k]*sum(normals[j][k] for j in ids) for k in range(3))
        total += abs(dot)
        positive += max(0,dot)
    return positive/total if total else 0


class Reader:
    def __init__(self, data):
        self.data = data

    def read(self, fmt, off):
        assert 0 <= off <= len(self.data)-struct.calcsize('<'+fmt), f'Out-of-range field {off:#x}'
        return struct.unpack_from('<'+fmt, self.data, off)

    def u32(self, off):
        return self.read('I', off)[0]

    def ptr(self, off):
        value = self.read('i', off)[0]
        assert value and 0 <= off+value < len(self.data), f'Invalid relative pointer {off:#x}'
        return off+value

    def string(self, off):
        start = self.ptr(off)
        end = self.data.find(b'\0', start, start+256)
        assert end >= start, f'Unterminated name {off:#x}'
        return self.data[start:end].decode('utf-8')

    def array(self, off, references=True):
        count = self.u32(off)
        assert count <= 4096
        if not count:
            assert self.u32(off+4) == 0
            return []
        start = self.ptr(off+4)
        self.read(f'{count}I', start)
        return [(self.ptr if references else self.u32)(start+4*i) for i in range(count)]

    def dictionary(self, off):
        count = self.u32(off)
        if not count:
            assert self.u32(off+4) == 0
            return {}
        start = self.ptr(off+4)
        assert self.data[start:start+4] == b'DICT'
        size, entries = self.read('II', start+4)
        assert count == entries and size == 28+16*count and count <= 4096
        for i in range(count+1):
            left, right = self.read('HH', start+16+16*i)
            assert left <= count and right <= count
        result = {self.string(start+36+16*i): self.ptr(start+40+16*i) for i in range(count)}
        assert len(result) == count
        return result


def verify_banner(data):
    r = Reader(data)
    assert data[:4] == b'CGFX' and r.read('HH', 4) == (0xfeff, 20)
    assert r.u32(12) == len(data) <= 0x80000
    assert data[20:24] == b'DATA'
    models = r.dictionary(28)
    assert set(models) == {'COMMON'}
    model = models['COMMON']
    assert r.u32(model) == 0x40000092 and data[model+4:model+8] == b'CMDL'
    meshes, shapes = r.array(model+180), r.array(model+196)
    materials = r.dictionary(model+188)
    assert len(meshes) == len(shapes) == len(materials) == 4, 'Use four atlas draws for this diorama'
    textures = r.dictionary(36)
    assert len(textures) == 2, 'Expected a shared diorama atlas and separate logo'
    profiles = []
    for name,texture in textures.items():
        assert r.u32(texture) == 0x20000011
        height,width = r.read('II',texture+24)
        fmt = r.u32(texture+52)
        assert fmt == 4, 'Use the physical-tested RGBA4 texture profile'
        assert width <= 256 and height <= 256, 'Do not bypass the reference texture size cap'
        assert r.u32(texture+40) == 1, 'Expected one complete mip level'
        pixel = r.ptr(texture+56)
        assert r.read('II',pixel) == (height,width)
        size = r.u32(pixel+8)
        start = r.ptr(pixel+12)
        assert size == width*height*2 and start+size <= len(data)
        profiles.append((width,height,fmt))
    assert sorted(profiles) == [(256,128,4),(256,256,4)], 'Keep package 5 texture dimensions and byte budget'
    for material in materials.values():
        assert r.u32(material+24) == 1, 'Keep the reference fragment-lighting material path'
        assert r.read('IIfII', material+260) == (0,2,0,2,0x10040), 'Keep reference culling state'
        fragment=r.ptr(material+648)
        for stage in range(6):
            combiner=fragment+44+stage*28
            constant,src_rgb,src_alpha,header,ops,rgb,alpha=r.read('IHHIIHH',combiner)
            assert header == 0x804F0000 | ((0xC0 if stage<4 else 0xD0)+stage*8)
            assert ops==0
            if stage==0:
                assert (src_rgb,src_alpha,rgb,alpha)==(0x030,0x030,1,1), 'Expected texture times vertex color'
            else:
                assert (src_rgb,src_alpha,rgb,alpha)==(0xFFF,0xFFF,0,0), 'Baked colors must not depend on HOME lighting'
    skel = r.ptr(model+224)
    assert r.u32(skel) == 0x2000000
    bones = r.dictionary(skel+24)
    # Authoring budget for this four-object scene, not a claimed CGFX limit.
    # Package 2 duplicated transforms for 75 material draws (78 bones).
    assert set(bones) == {'Scene root', 'Melee diorama', 'Final Destination',
                          'Fox 1', 'Fox 2', 'Melee logo', 'Banner Camera'}, 'Unexpected banner skeleton'
    bone_ids = {}
    for name, bone in bones.items():
        assert r.string(bone) == name
        assert not r.u32(bone+4) & 512, f'Skinning matrix in {name}'
        index = r.u32(bone+8)
        assert index not in bone_ids
        bone_ids[index] = name
        assert all(math.isfinite(x) for x in r.read('45f', bone+32))
        assert r.u32(bone+212) == (5 if name == 'Melee logo' else 0), 'Only the logo may billboard'
    logo = bones['Melee logo']
    assert r.u32(logo+212) == 5
    assert r.read('9f', logo+32) == (1,1,1,0,0,0,0,0,0), 'Logo must have identity TRS'
    assert r.ptr(logo+16) == r.ptr(skel+32), 'Logo must be a sibling of the world'
    bindings = {}
    winding = {}
    triangles = 0
    for mesh in meshes:
        assert r.u32(mesh) == 0x1000000
        shape_index, material_index = r.read('II', mesh+24)
        assert shape_index < len(shapes) and material_index < len(materials)
        shape = shapes[shape_index]
        assert r.u32(shape) == 0x10000001
        name = r.string(mesh+112)
        assert name in bones, f'Missing mesh-node binding: {name}'
        bindings[name] = shape
        attributes = r.array(shape+56)
        counts = []
        usages = []
        vectors = {}
        for attr in attributes:
            assert r.u32(attr) == 0x40000001, 'Expected independent vertex stream'
            usage = r.u32(attr+4)
            assert usage not in (7,8), 'Bone-index/weight stream is unsafe for this banner'
            usages.append(usage)
            count = r.u32(attr+20)
            stream = r.ptr(attr+24)
            fmt, components = r.read('II', attr+36)
            assert fmt == 0x1406, 'Use the tested float vertex profile'
            assert components == {0:3,1:3,3:3,4:2}[usage]
            unit = {0x1400:1,0x1401:1,0x1402:2,0x1406:4}[fmt]
            assert count % (components*unit) == 0 and stream+count <= len(data)
            assert r.read('f', attr+44)[0] == 1
            assert all(math.isfinite(v) for v in r.read(f'{count//4}f', stream))
            if usage in (0,1):
                values = r.read(f'{count//4}f',stream)
                vectors[usage] = list(zip(values[::3],values[1::3],values[2::3]))
            counts.append(count//(components*unit))
        assert set(usages) == {0,1,3,4} and len(set(counts)) == 1
        for primitive_set in r.array(shape+44):
            assert r.u32(primitive_set+8) == 0, 'Soft-skinned primitive set can crash HOME Menu'
            related = r.array(primitive_set, references=False)
            assert len(related) == 1 and bone_ids[related[0]] == name, 'Shape/mesh/bone mismatch'
            for primitive in r.array(primitive_set+12):
                for stream in r.array(primitive):
                    dtype = r.u32(stream)
                    size = r.u32(stream+8)
                    ptr = r.ptr(stream+12)
                    assert dtype in (0x1401,0x1403)
                    width = 1 if dtype == 0x1401 else 2
                    assert size % (width*3) == 0
                    indices = r.read(f'{size//width}{"B" if width == 1 else "H"}', ptr)
                    assert max(indices) < counts[0]
                    triangles += len(indices)//3
                    if name.startswith('Fox '):
                        fraction = outward_fraction(vectors[0],vectors[1],indices)
                        assert fraction > .9, f'Inside-out fighter geometry: {name}'
                        winding[name] = fraction
    animations = r.dictionary(28+9*8)
    assert set(animations) == {'COMMON'}
    animation = animations['COMMON']
    assert data[animation:animation+4] == b'CANM'
    assert r.string(animation+12) == 'SkeletalAnimation' and r.u32(animation+16) == 1
    frames = r.read('f', animation+20)[0]
    assert 0 < frames <= 600
    members = r.dictionary(animation+24)
    assert set(members) == {'Fox 1','Fox 2'}, 'Animate each complete fighter once'
    for name, member in members.items():
        assert name in bindings and r.string(member+4) == name, 'Animation must target a mesh node'
        assert r.u32(member+16) == 5, 'Only ordinary Transform animation is supported'
        flags = r.u32(member)
        assert flags == 0, 'Use complete TRS curves, matching the animated reference'
        for off, constant, ignore, expected in zip((20,24,28,32,36,40,48,52,56),
                (6,7,8,9,10,11,13,14,15), (16,17,18,19,20,21,23,24,25),
                r.read('9f',bones[name]+32)):
            if flags & (1 << ignore):
                continue
            if flags & (1 << constant):
                assert math.isfinite(r.read('f', member+off)[0])
                continue
            curve = r.ptr(member+off)
            start, end = r.read('ff', curve)
            assert 0 <= start < end <= frames+.001
            count = r.u32(curve+16)
            assert 1 <= count <= 64
            for i in range(count):
                segment = r.ptr(curve+20+4*i)
                a, b, mode, keys, speed = r.read('ffIIf', segment)
                assert start <= a < b <= end and mode == 196 and 2 <= keys <= 4096
                values = r.read(f'{keys*2}f', segment+20)
                assert all(math.isfinite(v) for v in values)
                assert all(abs(v-expected)<.0001 for v in values[1::2]), 'Fighter poses must remain fixed'
                times = values[::2]
                assert all(x < y for x,y in zip(times,times[1:]))
                assert abs(times[0]-a) < .001 and abs(times[-1]-b) < .001 and speed > 0
    return dict(meshes=len(meshes), materials=len(materials), textures=2,
                scene_data_bytes=r.u32(24), bones=len(bones), triangles=triangles,
                animation_members=len(members), frames=frames, rigid_only=True,
                mesh_bindings_verified=True, serialized_curves_verified=True,
                baked_color_combiners_verified=True, fighter_outward_fraction=winding,
                logo_resolution=[256,128], logo_format='RGBA4', fixed_poses_verified=True)
