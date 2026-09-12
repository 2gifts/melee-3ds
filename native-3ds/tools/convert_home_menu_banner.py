"""Convert the locally authored glTF into a size-bounded HOME Menu CGFX.

Uses the separately downloaded, pinned skyfloogle/pycgfx converter. Assets
remain private. Use the float RGB streams of hardware-tested banners.
"""
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'build/home-menu/art'


def main():
    sys.path[:0] = [str(ROOT / '.toolchain/home-menu/python'),
                    str(ROOT / '.toolchain/home-menu/pycgfx')]
    import main as converter
    import gltflib
    from cgfx.mtob import ColorFloat, FragmentLightingFlags
    from cgfx.primitives import DataType, VertexAttributeUsage as Usage
    from cgfx.sobj import BillboardMode

    source = gltflib.GLTF.load(str(ART / 'scene.gltf'), load_file_resources=True)
    assert not source.model.skins, 'Soft skins can crash physical HOME Menu'
    assert all(n.skin is None for n in source.model.nodes)
    # Follow the reference converter's two-sided geometry and render state.
    # Four atlas draws keep the duplicated stage/logo geometry affordable.
    banner = converter.convert_gltf(source)
    for model_name in banner.data.models:
        model = banner.data.models[model_name]
        for name in model.materials:
            material = model.materials[name]
            material.material_color.constant[0] = ColorFloat(0, 0, 0, 1)
            # Captured vertices carry our baked ambient/directional shading.
            # HOME's light rig must not multiply that color by zero. Keep
            # the proven shader/material layout, but pass texture * vertex
            # color through the remaining TEV stages unchanged.
            diffuse = material.fragment_shader.texture_combiners[1]
            diffuse.src_rgb, diffuse.combine_rgb = 0xFFF, 0
            specular = material.fragment_shader.texture_combiners[2]
            specular.src_rgb, specular.combine_rgb = 0xFFF, 0
            material.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
            material.fragment_shader.fragment_lighting_table.distribution_0_sampler = None
        for mesh in model.meshes.data.contents:
            shape = model.shapes.data.contents[mesh.shape_index]
            bone_ids = {b for p in shape.primitive_sets.data.contents for b in p.related_bones.data.contents}
            assert len(bone_ids) == 1
            mesh.mesh_node_name = model.skeleton.bones[next(iter(bone_ids))].name
        for name in model.skeleton.bones:
            bone = model.skeleton.bones[name]
            bone.billboard_mode = BillboardMode.Off
            if name == 'Melee logo':
                bone.billboard_mode = BillboardMode.YAxial
        for shape in model.shapes.data.contents:
            assert all(p.skinning_mode == 0 for p in shape.primitive_sets.data.contents)
            for attr in shape.vertex_attributes.data.contents:
                assert attr.usage not in (Usage.BoneIndex, Usage.BoneWeight)
                assert attr.format_type == DataType.Float
                assert attr.scale == 1.0
                if attr.usage == Usage.Color:
                    assert attr.components_count == 3
    raw = converter.write(banner)
    from verify_home_banner import verify_banner
    validation = verify_banner(raw)
    if len(raw) > 0x80000:
        raise RuntimeError(f'HOME Menu CGFX exceeds 512 KiB: {len(raw)}')
    (ART / 'banner.cgfx').write_bytes(raw)
    report = dict(cgfx_bytes=len(raw), limit_bytes=0x80000,
                  vertex_profile='float RGB, rigid grouped meshes',
                  validation=validation)
    (ART / 'conversion-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
