"""Verify structural cache dependencies against the actual color evaluators."""
import json,subprocess
from build import ROOT,local_clang
out=ROOT/'build/material-program-host';out.mkdir(parents=True,exist_ok=True)
source=(ROOT/'port/engine/gx.c').read_text()
reference=source[source.index('static float konst('):source.index('static void raster_color(float*')]
reference+=source[source.index('static float color_arg('):source.index('static void triangle(')]
(out/'material-reference.inc').write_text(reference)
trace=json.loads((ROOT/'build/cpu-fallback-investigation/materials.json').read_text());fixtures=[]
for key,_ in trace['usage']:
    m=trace['materials'][key];alpha=[]
    for ch in range(2):
        a=m['channel_configuration'][(2+ch)*6:(3+ch)*6]
        alpha.append(m['material'][ch*4+3]/255 if not a[0] and (a[2]==0 or not m['descriptors'][11+ch]) else -1)
    cfg=m['tev_configuration']
    fixtures.append('{'+str(m['num_stages'][0])+',{'+','.join('{'+','.join(map(str,cfg[i*30:(i+1)*30]))+'}' for i in range(16))+'},'+
        '{'+','.join(map(str,m['tev_color']))+'},{'+','.join(map(str,m['konst_color']))+'},{'+','.join(map(str,alpha))+'}}')
(out/'material-fixtures.inc').write_text('static const Fixture fixtures[]={'+','.join(fixtures)+'};\n')
exe=out/'test.exe'
subprocess.run([local_clang(),'-O2','-ffp-contract=off','-I'+str(out),str(ROOT/'tests/material_program_tests.c'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True)
(out/'result.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
