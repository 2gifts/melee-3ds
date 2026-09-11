"""Regression for stereo views, asynchronous disc loading and texture visibility."""
import argparse,json,subprocess,sys
from gameplay_test import ROOT
from profile_switch import set_word
from texture_visibility_test import read
from stage_sweep import exit_training

OUT=ROOT/'build/update8-qa'
def run(tool,*args,label=None):
    name=label or tool.removesuffix('.py')
    with (OUT/(name+'.log')).open('w') as f:
        subprocess.run([sys.executable,str(ROOT/'tools'/tool),*map(str,args)],stdout=f,stderr=subprocess.STDOUT,check=True)
    print('Passed '+name,flush=True)
def keep(name,as_name=None):(OUT/(as_name or name)).write_bytes((ROOT/'build'/name).read_bytes())

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--resume-at',choices=('stream','stages'));args=ap.parse_args()
    OUT.mkdir(exist_ok=True)
    if args.resume_at:
        if args.resume_at=='stream':
            run('stereo_pressure_test.py','--phases','stream,texture',label='stereo-pressure-final')
            command=json.loads((OUT/'stereo-pressure-command.json').read_text())
            other=json.loads((ROOT/'build/stereo-pressure-test.json').read_text())
            (OUT/'stereo-pressure-test.json').write_text(json.dumps(command+other,indent=2))
        finish();return
    set_word('mp_test_stereo_slider',0);set_word('framebuffer_range_disable',0)
    if read(('mp_async_file_checks',))['mp_async_file_checks']<64:run('async_file_test.py')
    keep('async-file-test.json')
    run('command_buffer_test.py');keep('command-buffer-test.json')
    run('display_mode_test.py','--minimum-side-pixels',100);keep('display-mode-test.json')
    run('layered_gpu_test.py');keep('layered-gpu-test.json')
    run('efb_rgb565_test.py','--copies',64);keep('efb-rgb565-live-test.json')
    run('peach_turnip_test.py','--prepare','--count',8,'--label','update8-turnips')
    run('native_geometry_test.py','--seconds',3,label='mono-cache');keep('native-geometry-test.json','mono-cache.json')
    set_word('mp_test_stereo_slider',1000)
    run('stereo_gpu_test.py');keep('stereo-gpu-test.json')
    run('peach_turnip_test.py','--count',8,'--label','update8-stereo-turnips')
    run('native_geometry_test.py','--seconds',3,label='stereo-cache');keep('native-geometry-test.json','stereo-cache.json')
    run('stereo_pressure_test.py');keep('stereo-pressure-test.json')
    finish()

def finish():
    set_word('mp_test_stereo_slider',1000)
    initial=read(('texture_content_checks','texture_dirty_invalidated'))
    set_word('texture_content_validate',1)
    try:
        run('stage_sweep.py','--stages',','.join(map(str,range(29))),'--seconds',2,'--character-offset',0,
            '--label','update8-all-stage','--image-directory','update8-stage-sweep',label='stereo-all-stage')
    finally:set_word('texture_content_validate',0)
    final=read(('texture_content_checks','texture_dirty_invalidated'))
    (OUT/'texture-visibility-checks.json').write_text(json.dumps({'before':initial,'after':final},indent=2))
    stages=json.loads((ROOT/'build/update8-all-stage.json').read_text());assert len(stages)==29
    assert {s['character_icon'] for s in stages}==set(range(25))
    for s in stages:
        m=s['profile']['final_memory'];assert m['geometry_bytes']<=12*1024*1024 and m['mp_native_heap_available']>=8*1024*1024,s
    keep('update8-all-stage.json')
    run('stereo_gameplay_test.py');keep('stereo-gameplay-test.json');keep('cpu-attack-test.json','pokefloats-combat.json')
    exit_training();run('select_test_stage.py',8,'--character',10,'--cpu',16,label='select-fountain')
    set_word('mp_test_stereo_slider',700)
    run('fox_link_stress.py');keep('fox-link-special-stress.json')
    run('cpu_attack_test.py','--seconds',120,label='fountain-combat');keep('cpu-attack-test.json','fountain-combat.json')
    run('audio_capture_test.py');keep('audio-capture-test.json')
    set_word('mp_test_stereo_slider',0)
    run('verify_live_image.py');keep('live-image-verification.json')
    print('Update 8 rendering, I/O, all-stage stereo and gameplay checks passed.',flush=True)

if __name__=='__main__':main()
