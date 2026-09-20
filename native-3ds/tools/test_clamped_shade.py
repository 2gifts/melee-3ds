"""Compare the candidate against the actual scalar TEV evaluator and captures."""
import hashlib,json,subprocess
from build import ROOT,local_clang

out=ROOT/'build/clamped-shade-host';out.mkdir(parents=True,exist_ok=True)
source=(ROOT/'port/engine/gx.c').read_text()
reference=source[source.index('static float konst('):source.index('static void raster_color(float*')]
reference+=source[source.index('static float color_arg('):source.index('static void triangle(')]
reference+=source[source.index('static void shade_constant_mask('):source.index('static void canonical_shade_key(')]
(out/'clamped-reference.inc').write_text(reference)
trace=json.loads((ROOT/'build/cpu-fallback-investigation/materials.json').read_text())
fixtures=[]
for key,_ in trace['usage']:
    m=trace['materials'][key];alpha=[]
    for ch in range(2):
        a=m['channel_configuration'][(2+ch)*6:(3+ch)*6]
        alpha.append(m['material'][ch*4+3]/255 if not a[0] and (a[2]==0 or not m['descriptors'][11+ch]) else -1)
    cfg=m['tev_configuration']
    fixtures.append('{'+str(m['num_stages'][0])+',{'+','.join('{'+','.join(map(str,cfg[i*30:(i+1)*30]))+'}' for i in range(16))+'},'+
        '{'+','.join(map(str,m['tev_color']))+'},{'+','.join(map(str,m['konst_color']))+'},{'+','.join(map(str,alpha))+'}}')
(out/'clamped-fixtures.inc').write_text('static const Fixture fixtures[]={'+','.join(fixtures)+'};\n')
exe=out/'test.exe'
subprocess.run([local_clang(),'-O2','-ffp-contract=off','-I'+str(out),str(ROOT/'tests/clamped_shade_tests.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True)
(out/'result.txt').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='');result.check_returncode()
(out/'evidence.json').write_text(json.dumps(dict(passed=True,
    trace_elf_sha256=trace['elf_sha256'],header_sha256=hashlib.sha256((ROOT/'port/engine/clamped_shade.h').read_bytes()).hexdigest(),
    evaluator_and_cache_source_sha256=hashlib.sha256((ROOT/'port/engine/gx.c').read_bytes()).hexdigest(),
    scope='Differential CPU algebra checks; GPU precision and physical performance unverified'),indent=2)+'\n')
