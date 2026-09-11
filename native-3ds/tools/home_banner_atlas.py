"""Combine the private diorama into four rigid, textured HOME Menu draws.

Keep texture resolution and vertex shading. Split triangles at clamp boundaries
before remapping UVs, so an atlas cannot introduce seams or sample another tile.
"""
import numpy as np
from PIL import Image


def split_clamped_triangle(vertices):
    """Vertices contain position, normal, UV and color; UV occupies columns 6:8."""
    polygons = [vertices]
    for axis in (6, 7):
        for boundary in (0., 1.):
            next_polygons = []
            for polygon in polygons:
                distances = polygon[:, axis] - boundary
                if not (np.any(distances < 0) and np.any(distances > 0)):
                    next_polygons.append(polygon)
                    continue
                sides = [[], []]
                for i, vertex in enumerate(polygon):
                    previous = polygon[i-1]
                    a, b = previous[axis]-boundary, vertex[axis]-boundary
                    if a*b < 0:
                        cut = previous + (vertex-previous)*(-a/(b-a))
                        cut[axis] = boundary
                        sides[0].append(cut); sides[1].append(cut)
                    if b <= 0: sides[0].append(vertex)
                    if b >= 0: sides[1].append(vertex)
                next_polygons.extend(np.asarray(side) for side in sides if len(side) >= 3)
            polygons = next_polygons
    return [np.asarray((p[0], p[i], p[i+1])) for p in polygons for i in range(1, len(p)-1)]


def compact_scene(scene, output):
    def read(index):
        accessor = scene.accessors[index]
        view = scene.views[accessor['bufferView']]
        dtype = {5126:'<f4', 5123:'<u2'}[accessor['componentType']]
        size = {'SCALAR':1, 'VEC2':2, 'VEC3':3, 'VEC4':4}[accessor['type']]
        return np.frombuffer(scene.blob, dtype=dtype, count=accessor['count']*size,
                             offset=view['byteOffset']).reshape(-1, size).copy()

    old_meshes, old_materials = scene.meshes, scene.materials
    old_images, old_textures = scene.images, scene.textures
    tiles, images = {}, {}
    for index, material in enumerate(old_materials):
        if material['name'] == 'Logo': continue
        texture = material['pbrMetallicRoughness'].get('baseColorTexture')
        if texture:
            uri = old_images[old_textures[texture['index']]['source']]['uri']
            images[index] = Image.open(output/uri).convert('RGBA')
        else:
            images[index] = Image.new('RGBA', (2, 2), 'white')
    width, gutter, x, y, row = 256, 2, 0, 0, 0
    for index in sorted(images, key=lambda i: (-images[i].height, i)):
        image = images[index]; w, h = image.size
        if x+w+gutter*2 > width: x, y, row = 0, y+row, 0
        tiles[index] = (x+gutter, y+gutter, w, h)
        x += w+gutter*2; row = max(row, h+gutter*2)
    height = 1 << (y+row-1).bit_length()
    assert height <= 256, 'Diorama texture atlas exceeded its authoring budget'
    atlas = Image.new('RGBA', (width, height))
    for index, (x, y, w, h) in tiles.items():
        pixels = np.asarray(images[index])
        padded = np.pad(pixels, ((gutter,gutter),(gutter,gutter),(0,0)), mode='edge')
        atlas.paste(Image.fromarray(padded), (x-gutter,y-gutter))
    atlas.save(output/'diorama-atlas.png')

    # Read all old accessors before replacing their backing buffer.
    geometry = []
    before_triangles = 0
    for mesh in old_meshes:
        parts = []
        for primitive in mesh['primitives']:
            attrs = primitive['attributes']
            vertices = np.concatenate([read(attrs[k]) for k in
                ('POSITION','NORMAL','TEXCOORD_0','COLOR_0')], axis=1)
            indices = read(primitive['indices']).reshape(-1,3)
            before_triangles += len(indices)
            material = primitive['material']
            if mesh['name'] == 'Melee logo':
                parts.extend(vertices[indices])
                continue
            x, y, w, h = tiles[material]
            textured = 'baseColorTexture' in old_materials[material]['pbrMetallicRoughness']
            for indices3 in indices:
                triangle = vertices[indices3]
                if textured:
                    triangles = split_clamped_triangle(triangle)
                else:
                    triangle[:,6:8] = .5
                    triangles = [triangle]
                for piece in triangles:
                    piece[:,6:8] = (np.clip(piece[:,6:8],0,1)*[w,h]+[x,y])/[width,height]
                    parts.append(piece)
        geometry.append(np.concatenate(parts))

    animation_data = [(sampler, read(sampler['input']), read(sampler['output']),
                       scene.accessors[sampler['output']]['type'])
                      for animation in scene.animations for sampler in animation['samplers']]
    scene.blob = bytearray(); scene.views = []; scene.accessors = []
    scene.meshes = []; scene.materials = []
    logo_index = next(i for i,m in enumerate(old_materials) if m['name'] == 'Logo')
    logo_uri = old_images[old_textures[old_materials[logo_index]['pbrMetallicRoughness']['baseColorTexture']['index']]['source']]['uri']
    scene.images = [dict(uri='diorama-atlas.png'),dict(uri=logo_uri)]
    scene.textures = [dict(source=0,sampler=0),dict(source=1,sampler=0)]
    for mesh, vertices in zip(old_meshes, geometry):
        node = next(n for n in scene.nodes if n.get('mesh') is not None and n['name'] == mesh['name'])
        material = len(scene.materials)
        is_logo = mesh['name'] == 'Melee logo'
        scene.materials.append(dict(name='Logo' if is_logo else mesh['name'],
            pbrMetallicRoughness=dict(metallicFactor=0,roughnessFactor=1,
                baseColorTexture=dict(index=1 if is_logo else 0)),
            alphaMode='MASK',alphaCutoff=.3,
            doubleSided=is_logo or mesh['name'] == 'Final Destination'))
        unique, indices = np.unique(vertices.astype('<f4'),axis=0,return_inverse=True)
        assert len(unique) < 65536
        attrs = dict(POSITION=scene.data(unique[:,:3],'VEC3'),
            NORMAL=scene.data(unique[:,3:6],'VEC3'),TEXCOORD_0=scene.data(unique[:,6:8],'VEC2'),
            COLOR_0=scene.data(unique[:,8:11],'VEC3'))
        node['mesh'] = len(scene.meshes)
        scene.meshes.append(dict(name=mesh['name'],primitives=[dict(attributes=attrs,
            indices=scene.data(indices,'SCALAR',5123),material=material,mode=4)]))
    for sampler, times, values, kind in animation_data:
        sampler['input'] = scene.data(times.reshape(-1),'SCALAR')
        sampler['output'] = scene.data(values,kind)
    return dict(original_draws=sum(len(m['primitives']) for m in old_meshes),draws=4,
                original_triangles=before_triangles,
                split_triangles=sum(len(v)//3 for v in geometry),atlas_size=[width,height],
                materials=4,textures=2)
