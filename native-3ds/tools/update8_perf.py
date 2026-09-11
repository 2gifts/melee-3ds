"""Measure old/new frame texture invalidation in one settled mono scene."""
import json,shutil,subprocess,sys
from gameplay_test import ROOT
from profile_switch import set_word
from stage_sweep import exit_training
import select_test_stage as select

def main():
    set_word('mp_test_stereo_slider',0)
    if 'hand' not in select.observe():exit_training()
    subprocess.run([sys.executable,str(ROOT/'tools/select_test_stage.py'),
                    '11','--character','22','--cpu','16'],check=True)
    select.act(frames=120)
    path=ROOT/'build/profile-framebuffer_range_disable.json'
    if path.exists():shutil.copy2(path,ROOT/'build/update8-framebuffer-stadium-initial.json')
    subprocess.run([sys.executable,str(ROOT/'tools/profile_switch.py'),
                    'framebuffer_range_disable','--seconds','8'],check=True)
    rows=json.loads(path.read_text())
    assert all(r['same_scene'] for r in rows),rows
    result={'scene':'Venom, Mr. Game & Watch / Link, Training Stand, mono 4:3',
            'environment':'Azahar 2126.1, 300% CPU, uncapped; not physical hardware FPS',
            'old_mode':1,'new_mode':0,
            'windows':[{'mode':r['value'],'render_rate':r['rates']['render'],
                        'source_hashes_per_render':r['texture_work_per_render']['texture_hash_checks'],
                        'source_hash_bytes_per_render':r['texture_work_per_render']['texture_hash_bytes'],
                        'texture_lookup_ms_per_render':r['phases']['texture_lookup']['estimated_ms_per_render'],
                        'native_requests_per_render':r['native_geometry_work_per_render']['requests'],
                        'source_bytes_per_render':r['source_checks_per_render']['geometry_source_bytes']}
                       for r in rows]}
    (ROOT/'build/update8-qa/texture-visibility-performance.json').write_text(json.dumps(result,indent=2)+'\n')
    shutil.copy2(path,ROOT/'build/update8-qa/profile-framebuffer_range_disable.json')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
