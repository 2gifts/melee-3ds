"""Check the decal matcher and its fragment algebra against the cursor's GX program."""
import json,subprocess
from build import ROOT,local_clang

source=(ROOT/'port/engine/gx.c').read_text()
reference=source[source.index('static float konst('):source.index('static void raster_color(float*')]
reference+=source[source.index('static float color_arg('):source.index('static void triangle(')]
reference=reference.replace('if(a==8||a==9)return 1;','if(a==8||a==9)return reference_texture[a==8?k:3];')
reference=reference.replace('if(a==4)return 1;','if(a==4)return reference_texture[3];')
reference=reference.replace('u32*s=tev_configuration[i];float value','u32*s=tev_configuration[i];reference_texture=reference_textures[s[1]&7];float value')
gen=ROOT/'build/generated';gen.mkdir(parents=True,exist_ok=True)
(gen/'cursor_reference.inc').write_text(reference)
rows=lambda c,n:'{'+','.join('{'+','.join(map(str,(c+[0]*480)[i*30:(i+1)*30]))+'}' for i in range(n))+'}'
colors=lambda h:'{'+','.join('{'+','.join(str(int(h[(i*4+j)*2:(i*4+j)*2+2],16)) for j in range(4))+'}' for i in range(4))+'}'
cursor=json.loads((ROOT/'tests/fixtures/cursor-material.json').read_text())
shields=json.loads((ROOT/'tests/fixtures/shield-materials.json').read_text())
(gen/'cursor_program.inc').write_text(
    'static const unsigned captured[16][30]='+rows(cursor['tev_configuration'],16)+';\n'
    'static const GXColor captured_colors[4]='+colors(cursor['tev_color'])+';\n'
    'static const GXColor captured_konst[4]='+colors(cursor['konst_color'])+';\n')
(gen/'cursor_shield_programs.inc').write_text('static const unsigned captured_shield[2][16][30]={'+','.join(rows(d['tev_configuration'],16) for d in shields)+'};\n')
exe=ROOT/'build/cursor-material-tests.exe'
subprocess.run([local_clang(),'-O2','-Wall','-I'+str(gen),str(ROOT/'tests/cursor_material_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
