"""Convert the locally authored glTF into a size-bounded HOME Menu CGFX.

Uses the separately downloaded, pinned skyfloogle/pycgfx converter. Assets
remain private. Quantization affects the banner only, never game geometry.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'build/home-menu/art'


def main():
    sys.path[:0] = [str(ROOT / '.toolchain/home-menu/python'),
                    str(ROOT / '.toolchain/home-menu/pycgfx')]
    import main as converter
    import gltflib
    from cgfx.mtob import CullMode
    from cgfx.primitives import DataType, VertexAttributeUsage as Usage
    from cgfx.sobj import BillboardMode

    source = gltflib.GLTF.load(str(ART / 'scene.gltf'), load_file_resources=True)
    # PICA can disable culling directly. Avoid pycgfx's doubled geometry.
    two_sided = {m.name for m in source.model.materials if m.doubleSided}
    for material in source.model.materials:
        material.doubleSided = False
    banner = converter.convert_gltf(source)
    saved = 0
    max_position_error = 0.0
    for model_name in banner.data.models:
        model = banner.data.models[model_name]
        for name in model.materials:
            material = model.materials[name]
            if name in two_sided or name == 'Logo':
                material.rasterization.cull_mode = CullMode.Never
                material.rasterization.command.param = CullMode.Never
            if name == 'Logo':
                # Typography stays bright regardless of HOME's light rig.
                material.flags = 0
                for combiner in material.fragment_shader.texture_combiners[1:3]:
                    combiner.src_rgb = 0xF
                    combiner.combine_rgb = 0
        for mesh in model.meshes.data.contents:
            mesh.mesh_node_name = mesh.name
        for name in model.skeleton.bones:
            bone = model.skeleton.bones[name]
            if name == 'Melee logo':
                bone.billboard_mode = BillboardMode.YAxial
        for shape in model.shapes.data.contents:
            for attr in shape.vertex_attributes.data.contents:
                if attr.format_type != DataType.Float:
                    continue
                values = np.frombuffer(attr.vertex_stream_data, dtype='<f4')
                if not len(values):
                    continue
                assert np.isfinite(values).all()
                if attr.usage in (Usage.Color, Usage.BoneWeight):
                    assert values.min() >= 0 and values.max() <= 1
                    scale, dtype, fmt = 1/255, 'u1', DataType.UByte
                elif attr.usage == Usage.Normal:
                    # Original envelope normals can be unnormalized.
                    v = values.reshape(-1, attr.components_count).copy()
                    v /= np.maximum(np.linalg.norm(v, axis=1), 1e-8)[:, None]
                    values = v.ravel()
                    scale, dtype, fmt = 1/127, 'i1', DataType.Byte
                else:
                    maximum = max(float(np.abs(values).max()), 1e-8)
                    scale = 2 ** math.ceil(math.log2(maximum/32760))
                    dtype, fmt = '<i2', DataType.Short
                packed = np.rint(values / scale).astype(dtype)
                if attr.usage == Usage.Position:
                    max_position_error = max(max_position_error,
                        float(np.abs(packed.astype('f4')*scale-values).max()))
                saved += len(attr.vertex_stream_data) - packed.nbytes
                attr.vertex_stream_data = packed.tobytes()
                attr.format_type, attr.scale = fmt, scale
    raw = converter.write(banner)
    if len(raw) > 0x80000:
        raise RuntimeError(f'HOME Menu CGFX exceeds 512 KiB: {len(raw)}')
    (ART / 'banner.cgfx').write_bytes(raw)
    report = dict(cgfx_bytes=len(raw), limit_bytes=0x80000,
                  attribute_bytes_saved=saved,
                  max_position_quantization_error=max_position_error)
    (ART / 'conversion-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
