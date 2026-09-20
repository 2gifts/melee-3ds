"""Validate the separate physical measurement 3DSX and record its provenance."""
import hashlib,json
from pathlib import Path
from build import ROOT
from be8_image import ElfImage
from test_be8_image import check
from package import validate_3dsx

out=ROOT/'build/game-feasibility-console'
binary=ROOT/'dist/feasibility/3ds/melee-profile/melee-profile.3dsx'
info=check(out/'melee-linked.elf',out/'melee.elf',binary)
(out/'image-validation.json').write_text(json.dumps(info,indent=2)+'\n')
labels=ElfImage((out/'melee.elf').read_bytes()).symbols.keys()
assert {'mp_probe_request','mp_probe_scene','mp_probe_rows'}.issubset(labels)
assert not {'mp_test_control','mp_test_stereo_slider','mp_banner_capture','shade_program_disable','mp_test_capture','__ubsan_handle_type_mismatch_v1'}&labels
assert binary.with_suffix('.smdh').read_bytes()[:4]==b'SMDH'
manifest=dict(version='feasibility-capture-1',base_runtime=21,
 scope='Measurement only; no simulation/rendering omissions, physical controls. Profiling affects timing.',
 log_header='Melee feasibility capture 1 - update 21 baseline, sparse instrumentation, physical controls',
 log_path='/3ds/melee/feasibility.log',
 elf_sha256=hashlib.sha256((out/'melee.elf').read_bytes()).hexdigest(),binary=validate_3dsx(binary),
 smdh_sha256=hashlib.sha256(binary.with_suffix('.smdh').read_bytes()).hexdigest(),
 sample_stride=7,frames_per_window=120,windows_per_encounter=3,hardware_validated=False,image_validation=info,
 source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in
 ('port/engine/feasibility.c','port/engine/feasibility.h','tools/feasibility_overlay.py','tools/engine_build.py','tools/build_game.py','port/3ds/game.c','port/3ds/log_io.c')})
(binary.parent/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({k:v for k,v in manifest.items() if k!='source_sha256'},indent=2))
