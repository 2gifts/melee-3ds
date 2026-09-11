"""Run the stereo update's live renderer and gameplay regressions serially."""
import json,subprocess,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def run(name,*args):
    with (ROOT/'build'/('update15-'+name+'.log')).open('w') as log:
        subprocess.run([sys.executable,str(ROOT/'tools'/args[0]),*args[1:]],stdout=log,stderr=subprocess.STDOUT,check=True)
    print(name+' passed',flush=True)

def main():
    run('setup','performance_match_test.py')
    run('render-ab','render_update15_test.py')
    run('shader-qa','shader_gpu_test.py')
    run('stereo-qa','stereo_gpu_test.py')
    run('shield-qa','shield_gpu_test.py')
    run('layered-qa','layered_gpu_test.py')
    run('command-qa','command_buffer_test.py')
    run('image-qa','verify_live_image.py')
    import select_test_stage as select
    import bottom_screen_test as bottom
    from menu_display_pause_test import clock_state,test_pause
    bottom.OUT=ROOT/'build/update15-qa'
    if clock_state()['pause_flags']:select.act(0x1000,3);select.act(frames=25)
    test_pause()
    (bottom.OUT/'renderer-suite.json').write_text(json.dumps({'passed':True,'tests':['render-ab','shader','stereo','shield','layered','command','image','pause']},indent=2)+'\n')

if __name__=='__main__':main()
