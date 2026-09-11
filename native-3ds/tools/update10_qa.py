"""Bounded shield-color regression suite in a freshly selected Training match."""
import json,subprocess,sys
import numpy as np
from PIL import Image
from gameplay_test import ROOT
from profile_switch import set_word
from texture_visibility_test import read

OUT=ROOT/'build/update10-qa'
def run(tool,*args):
    with (OUT/(tool+'.log')).open('w') as log:
        subprocess.run([sys.executable,str(ROOT/'tools'/tool),*map(str,args)],stdout=log,stderr=subprocess.STDOUT,check=True)
    print('Passed '+tool,flush=True)
def keep(name):
    (OUT/name).write_bytes((ROOT/'build'/name).read_bytes())
def main():
    OUT.mkdir(exist_ok=True)
    set_word('mp_test_stereo_slider',0)
    run('shield_gpu_test.py');keep('shield-gpu-test.json')
    run('layered_gpu_test.py');keep('layered-gpu-test.json')
    colors=[]
    for stereo in (0,1000):
        set_word('mp_test_stereo_slider',stereo)
        for start in (False,True):
            label=f'update10-shield-{stereo}-'+('start' if start else 'held')
            run('trace_shield_material.py','--label',label,*(['--start-effect'] if start else []));keep(label+'.json')
            fixture=json.loads((ROOT/'build'/f'{label}.json').read_text());assert fixture['player_rgb']==0xf25959
            for eye in ('left','right') if stereo else ('left',):
                pixels=np.array(Image.open(ROOT/'build'/f'{label}-{eye}.png'))
                # Exclude the bottom HUD/avatar. The standing fighter's bubble
                # is above y=160; stage geometry is magenta and fails G>B*.8.
                rgb=pixels[:160].astype(float);red=(rgb[:,:,0]>rgb[:,:,1]*1.35)&(rgb[:,:,1]>rgb[:,:,2]*.8)&(rgb[:,:,0]>80)
                count=int(red.sum());assert count>30,(label,eye,count)
                colors.append({'label':label,'eye':eye,'red_shield_pixels':count});keep(f'{label}-{eye}.png')
    run('stereo_gpu_test.py');keep('stereo-gpu-test.json')
    run('peach_turnip_test.py','--prepare','--count',4,'--label','update10-turnips');keep('update10-turnips.json')
    run('native_geometry_test.py','--seconds',3);keep('native-geometry-test.json')
    run('cpu_attack_test.py','--seconds',30);keep('cpu-attack-test.json')
    run('verify_live_image.py');keep('live-image-verification.json')
    result={'shield_captures':colors,'gpu_cases':72,'cpu_tev_component_checks':800000,
            'engine_failed':read(('engine_failed',))['engine_failed'],'physical_hardware_verified':False}
    assert not result['engine_failed'];(OUT/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Update 10 shield, stereo, turnip, geometry and combat checks passed.',flush=True)
if __name__=='__main__':main()
