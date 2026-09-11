"""Check the captured GX program against the new fragment-combiner algebra."""
import json,subprocess
from build import ROOT,local_clang

source=(ROOT/'port/engine/gx.c').read_text()
reference=source[source.index('static float konst('):source.index('static void raster_color(float*')]
reference+=source[source.index('static float color_arg('):source.index('static void triangle(')]
reference=reference.replace('if(a==8||a==9)return 1;','if(a==8||a==9)return reference_texture[a==8?k:3];')
reference=reference.replace('if(a==4)return 1;','if(a==4)return reference_texture[3];')
reference=reference.replace('u32*s=tev_configuration[i];float value',
    'u32*s=tev_configuration[i];reference_texture=reference_textures[s[1]&7];float value')
fixture=json.loads((ROOT/'build/turnip-live-material.json').read_text())
gen=ROOT/'build/generated';gen.mkdir(exist_ok=True)
(gen/'layered_reference.inc').write_text(reference)
(gen/'turnip_material.inc').write_text('static const unsigned captured[16][30]={'+
    ','.join('{'+','.join(map(str,fixture['tev_configuration'][i*30:(i+1)*30]))+'}' for i in range(16))+'};\n')
exe=ROOT/'build/layered-material-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-I'+str(gen),str(ROOT/'tests/layered_material_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
