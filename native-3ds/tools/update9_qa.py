"""Complete update 9 checks after the continuous all-stage stereo sweep."""
import json, subprocess, sys
from gameplay_test import ROOT
from profile_switch import set_word
from texture_visibility_test import read
from stage_sweep import exit_training

OUT=ROOT/'build/update9-qa'

def run(tool,*args,label=None):
    name=label or tool.removesuffix('.py')
    with (OUT/(name+'.log')).open('w') as f:
        subprocess.run([sys.executable,str(ROOT/'tools'/tool),*map(str,args)],
                       stdout=f,stderr=subprocess.STDOUT,check=True)
    print('Passed '+name,flush=True)

def keep(name,as_name=None):
    (OUT/(as_name or name)).write_bytes((ROOT/'build'/name).read_bytes())

def collisions(label):
    names=('mp_collision_matrix_repairs','mp_collision_parent_repairs','mp_collision_matrix_failed','mp_collision_invalid_count')
    values={k:int.from_bytes(v.to_bytes(4,'little'),'big') for k,v in read(names).items()}
    (OUT/(label+'-collisions.json')).write_text(json.dumps(values,indent=2)+'\n')
    assert values['mp_collision_matrix_failed']==0,values
    assert values['mp_collision_invalid_count']==0,values
    return values

def main():
    OUT.mkdir(exist_ok=True)
    stages=json.loads((ROOT/'build/update9-all-stage.json').read_text())
    assert len(stages)==29 and {s['stage_index'] for s in stages}==set(range(29))
    assert {s['character_icon'] for s in stages}==set(range(25))
    for s in stages:
        m=s['profile']['final_memory']
        assert m['geometry_bytes']<=12*1024*1024 and m['mp_native_heap_available']>=8*1024*1024,s
    keep('update9-all-stage.json');collisions('after-stages')
    set_word('mp_test_stereo_slider',1000)
    run('stereo_gpu_test.py');keep('stereo-gpu-test.json')
    set_word('mp_test_stereo_slider',0)
    run('layered_gpu_test.py');keep('layered-gpu-test.json')
    run('peach_turnip_test.py','--prepare','--count',8,'--label','update9-turnips')
    run('native_geometry_test.py','--seconds',3,label='mono-cache');keep('native-geometry-test.json','mono-cache.json')
    set_word('mp_test_stereo_slider',1000)
    initial=read(('texture_content_checks','texture_dirty_invalidated'))
    set_word('texture_content_validate',1)
    try:
        run('peach_turnip_test.py','--count',8,'--label','update9-stereo-turnips')
        run('native_geometry_test.py','--seconds',3,label='stereo-cache');keep('native-geometry-test.json','stereo-cache.json')
        run('stereo_pressure_test.py');keep('stereo-pressure-test.json')
        run('efb_rgb565_test.py','--copies',32);keep('efb-rgb565-live-test.json')
    finally:set_word('texture_content_validate',0)
    final=read(('texture_content_checks','texture_dirty_invalidated'))
    (OUT/'texture-visibility-checks.json').write_text(json.dumps({'before':initial,'after':final},indent=2)+'\n')
    run('stereo_gameplay_test.py');keep('stereo-gameplay-test.json');keep('cpu-attack-test.json','pokefloats-combat.json')
    collisions('after-pokefloats')
    exit_training();run('select_test_stage.py',3,'--character',23,'--cpu',6,label='select-japes')
    set_word('mp_test_stereo_slider',1000)
    run('cpu_attack_test.py','--seconds',120,label='japes-combat');keep('cpu-attack-test.json','japes-combat.json')
    collisions('after-japes')
    run('profile_switch.py','stereo_order_reference','--seconds',6,label='stereo-command-performance')
    keep('profile-stereo_order_reference.json')
    run('audio_capture_test.py');keep('audio-capture-test.json')
    set_word('mp_test_stereo_slider',0)
    run('verify_live_image.py');keep('live-image-verification.json')
    print('Update 9 all-stage, stereo, collision and rendering checks passed.',flush=True)

if __name__=='__main__':main()
