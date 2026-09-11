"""Final combat and output checks after the uninterrupted stage rotation."""
from gameplay_test import ROOT
from stage_sweep import exit_training
from update6_qa import OUTPUT, run


def preserve(name):
    (OUTPUT/name).write_bytes((ROOT/'build'/name).read_bytes())


def main():
    exit_training()
    run('select_test_stage.py', 8, '--character', 10, '--cpu', 16, label='select-fountain-final')
    run('cpu_attack_test.py', '--seconds', 120, label='fountain-cpu-final')
    (OUTPUT/'fountain-cpu-final.json').write_bytes((ROOT/'build/cpu-attack-test.json').read_bytes())
    exit_training()
    run('select_test_stage.py', 18, '--character', 10, '--cpu', 16, label='select-stadium-final')
    run('display_mode_test.py', '--minimum-side-pixels', 100)
    preserve('display-mode-test.json')
    run('audio_capture_test.py')
    preserve('audio-capture-test.json')
    run('verify_live_image.py', label='verify-live-image-final')
    preserve('live-image-verification.json')
    print('Update 6 final combat, display, audio and image checks passed', flush=True)


if __name__ == '__main__': main()
