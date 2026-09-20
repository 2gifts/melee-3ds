import argparse,json,time
import numpy as np
from PIL import Image
from gameplay_test import ROOT
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read
p=argparse.ArgumentParser();p.add_argument('--output',default='build/update22-qa/clamped-shader');a=p.parse_args();out=ROOT/a.output;out.mkdir(parents=True,exist_ok=True)
set_word('mp_test_stereo_slider',1000);set_word('clamped_verify',1)
limit=time.monotonic()+60
while read(('clamped_verify',))['clamped_verify']:
    assert not read(('engine_failed',))['engine_failed'];assert time.monotonic()<limit;time.sleep(.25)
x=np.fromfile(SD/'clamped-fixture-0.bin',dtype=np.uint8).reshape(800,240,4);y=np.fromfile(SD/'clamped-fixture-1.bin',dtype=np.uint8).reshape(800,240,4)
d=np.abs(x.astype(int)-y.astype(int));report={'max_channel_error':int(d.max()),'changed_channels':int(np.count_nonzero(d)),'covered_pixels':int(np.count_nonzero(np.any(y[:,:,1:]!=0,axis=2))),'same_coverage':bool(np.array_equal(np.any(x[:,:,1:]!=0,axis=2),np.any(y[:,:,1:]!=0,axis=2)))}
for e in range(2):
    for label,z in [('reference',x),('candidate',y)]:Image.fromarray(np.rot90(z[e*400:(e+1)*400,:,::-1])).save(out/f'{label}-{e}.png')
(out/'comparison.json').write_text(json.dumps(report,indent=2));print(report,flush=True)
assert report['same_coverage'] and report['covered_pixels']>10000 and report['max_channel_error']<=2,report
