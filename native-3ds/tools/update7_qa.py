"""Gameplay, cache and rendering regression for the geometry CPU update."""
import json,subprocess,sys
from gameplay_test import ROOT
from stage_sweep import exit_training

OUTPUT=ROOT/'build/update7-qa'

def run(tool,*args,label=None):
    name=label or tool.removesuffix('.py')
    with (OUTPUT/(name+'.log')).open('w') as log:
        subprocess.run([sys.executable,str(ROOT/'tools'/tool),*map(str,args)],stdout=log,stderr=subprocess.STDOUT,check=True)
    print('Passed '+name,flush=True)

def preserve(name,dest=None):
    (OUTPUT/(dest or name)).write_bytes((ROOT/'build'/name).read_bytes())

def main():
    OUTPUT.mkdir(exist_ok=True)
    run('peach_turnip_test.py','--prepare','--fresh','--count',8,'--label','update7-turnips')
    run('native_memory_test.py');preserve('native-memory-test.json')
    run('layered_gpu_test.py');preserve('layered-gpu-test.json')
    run('native_geometry_test.py','--seconds',3,label='peach-cache');preserve('native-geometry-test.json','peach-cache.json')
    exit_training()
    run('select_test_stage.py',11,'--character',22,'--cpu',16,label='select-venom')
    run('native_geometry_test.py','--seconds',3,label='venom-cache');preserve('native-geometry-test.json','venom-cache.json')
    run('profile_render_detail.py','--seconds',8,label='venom-profile');preserve('render-detail-profile.json','venom-profile.json')
    exit_training()
    run('select_test_stage.py',8,'--character',10,'--cpu',16,label='select-fountain')
    run('fox_link_stress.py');preserve('fox-link-special-stress.json')
    run('cpu_attack_test.py','--seconds',120,label='fountain-combat');preserve('cpu-attack-test.json','fountain-combat.json')
    run('stage_sweep.py','--stages',','.join(map(str,range(29))),'--seconds',2,'--character-offset',0,
        '--label','update7-all-stage','--image-directory','update7-stage-sweep')
    stages=json.loads((ROOT/'build/update7-all-stage.json').read_text())
    assert {s['stage_index'] for s in stages}==set(range(29))
    assert {s['character_icon'] for s in stages}==set(range(25))
    for stage in stages:
        m=stage['profile']['final_memory']
        assert m['geometry_bytes']<=12*1024*1024 and m['mp_native_heap_available']>=8*1024*1024,stage
    preserve('update7-all-stage.json')
    exit_training()
    run('select_test_stage.py',18,'--character',10,'--cpu',16,label='select-stadium')
    run('command_buffer_test.py');preserve('command-buffer-test.json')
    run('display_mode_test.py','--minimum-side-pixels',100);preserve('display-mode-test.json')
    run('audio_capture_test.py');preserve('audio-capture-test.json')
    exit_training()
    run('select_test_stage.py',11,'--character',22,'--cpu',16,label='select-venom-native-reuse')
    run('native_geometry_test.py','--native-only','--seconds',3,label='venom-native-reuse');preserve('native-geometry-test.json','venom-native-reuse.json')
    run('verify_live_image.py');preserve('live-image-verification.json')
    run('summarize_geometry7_qa.py',label='summary')
    print('Update 7 gameplay, all-stage, cache, output and protected-image regression passed.',flush=True)

if __name__=='__main__':main()
