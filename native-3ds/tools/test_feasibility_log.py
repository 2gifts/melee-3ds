"""Keep profiler serialization/denominators trustworthy across the BE8 bridge."""
from analyze_feasibility import parse

fixture='''FeasibilityResult id=1 scene=2 frames=120 sampled=18 stride=7 ticks_hi=0 ticks_lo=81000000 errors=0 overflow=0 rows=1
FeasibilityRow id=1 domain=3 fn=abcdef01 context=0 calls=36 inclusive_hi=0 inclusive_lo=729000 self_hi=0 self_lo=364500
FeasibilityEnd id=1
'''
r=parse(fixture,{0xabcdef01:'function'})
assert len(r['windows'])==1 and not r['rejected']
w=r['windows'][0];assert w['mean_instrumented_frame_ms']==50/3
assert w['samples'][0]['own_ms_per_sampled_render']==0.5
assert w['samples'][0]['function']=='function'
for bad in [fixture.replace('rows=1','rows=2'),fixture.replace('errors=0','errors=1'),
            fixture.replace('overflow=0','overflow=1'),fixture.replace('sampled=18','sampled=0'),
            fixture.replace('FeasibilityEnd id=1\n',''),fixture.replace('self_lo=364500','self_lo=900000')]:
    r=parse(bad,{});assert not r['windows'] and r['rejected']
r=parse(fixture.replace('ticks_hi=0','ticks_hi=1'),{})
assert r['windows'][0]['ticks']==4294967296+81000000
print('PASS: 64-bit word reconstruction, sparse denominator, exact symbol mapping, corrupt and incomplete window rejection.')
