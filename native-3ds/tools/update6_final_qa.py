"""Regression checks for the final cache/turnip build using real game inputs."""
from gameplay_test import ROOT
from stage_sweep import exit_training
from update6_qa import run,OUTPUT


def preserve(name,dest=None):
    (OUTPUT/(dest or name)).write_bytes((ROOT/'build'/name).read_bytes())


def main():
    run('peach_turnip_test.py','--prepare','--fresh','--count',8,'--label','update6-turnips-final')
    run('layered_gpu_test.py');preserve('layered-gpu-test.json')
    run('raster_state_gpu_test.py');preserve('raster-state-gpu-test.json')
    run('geometry_cache_test.py','--seconds',5,label='peach-cache-final');preserve('geometry-cache-test.json','peach-cache-final.json')
    exit_training()
    run('select_test_stage.py',11,'--character',22,'--cpu',16,label='select-venom-final')
    run('profile_render_detail.py','--seconds',6,label='venom-profile-final');preserve('render-detail-profile.json','venom-profile-final.json')
    run('geometry_cache_test.py','--seconds',5,label='venom-cache-final');preserve('geometry-cache-test.json','venom-cache-final.json')
    exit_training()
    run('select_test_stage.py',18,'--character',10,'--cpu',16,label='select-stadium-output-final')
    run('display_mode_test.py','--minimum-side-pixels',100,label='display-output-final');preserve('display-mode-test.json','display-output-final.json')
    run('audio_capture_test.py',label='audio-output-final');preserve('audio-capture-test.json','audio-output-final.json')
    run('verify_live_image.py',label='protected-image-final');preserve('live-image-verification.json','protected-image-final.json')
    print('Final update 6 turnip, layered GPU, cache, display, audio and protected-image checks passed',flush=True)


if __name__=='__main__':main()
