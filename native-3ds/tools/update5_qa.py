"""Validate identical texture uploads across gameplay and ordinary scene changes."""
import json, subprocess, sys, time
from pathlib import Path
from gameplay_test import ROOT, exchange
from profile_switch import set_word
from texture_repack_live_test import counters
from stage_sweep import exit_training, metrics
import select_test_stage as select

OUTPUT = ROOT/'build/update5-qa'


def run(tool, *args):
    with (OUTPUT/(Path(tool).stem+'.log')).open('w') as log:
        subprocess.run([sys.executable, str(ROOT/'tools'/tool), *map(str,args)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print('Passed '+tool, flush=True)


def select_stage(index, fresh=False):
    old = sys.argv
    sys.argv = ['select_test_stage.py', str(index), '--character', '10', '--cpu', '16']
    if fresh: sys.argv.append('--fresh')
    try: select.main()
    finally: sys.argv = old


def capture(index):
    import numpy as np
    from PIL import Image
    run('capture_game.py')
    path = ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/user/sdmc/3ds/melee/engine-top.bgr'
    pixels = np.rot90(np.fromfile(path, dtype=np.uint8).reshape(400,240,3)[:,:,::-1])
    Image.fromarray(pixels).save(OUTPUT/f'stage-{index:02}.png')


def main():
    OUTPUT.mkdir(exist_ok=True)
    if '--finish-only' in sys.argv:
        finish_checks(); return
    select_stage(18, fresh=True)
    before = counters(); records = []
    try:
        set_word('texture_repack_validate', 1)
        for index in (18,6,8,24,11):
            if index != 18:
                exit_training(); select_stage(index)
            start = exchange(); time.sleep(4); end = exchange()
            assert not end['failed'] and end['simulation'] > start['simulation'], end
            records.append({'stage_index': index, 'initial': start, 'final': end, 'metrics': metrics()})
            capture(index)
            print('Passed texture validation stage '+str(index), flush=True)
        after = counters()
    finally:
        set_word('texture_repack_validate', 0)
    formats = []
    for index in range(16):
        count = after['texture_repack_format_checks'][index]-before['texture_repack_format_checks'][index]
        if not count: continue
        fast = (after['texture_repack_fast_ticks'][index]-before['texture_repack_fast_ticks'][index]) & 0xffffffff
        reference = (after['texture_repack_reference_ticks'][index]-before['texture_repack_reference_ticks'][index]) & 0xffffffff
        formats.append({'gx_format': index, 'identical_uploads': count,
                        'fast_mean_ms': fast/40500/count, 'reference_mean_ms': reference/40500/count,
                        'conversion_speed_ratio': reference/fast if fast else None})
    result = {'before': before, 'after': after, 'formats': formats, 'stages': records,
              'timing_scope': 'Both converters on identical live source textures, emulated ARM. Reference runs second. Not console FPS.'}
    (OUTPUT/'texture-validation.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({'paired_texture_results': formats}), flush=True)
    # Exercise fight input with the release conversion path, without validation overhead.
    run('cpu_attack_test.py', '--seconds', 30)
    (OUTPUT/'venom-cpu.json').write_bytes((ROOT/'build/cpu-attack-test.json').read_bytes())
    exit_training(); select_stage(8)
    run('cpu_attack_test.py', '--seconds', 60)
    (OUTPUT/'fountain-cpu.json').write_bytes((ROOT/'build/cpu-attack-test.json').read_bytes())
    finish_checks()


def finish_checks():
    # The side-pixel assertion needs scenery in the newly visible area.
    # Diet Fountain can expose almost entirely black space after a KO.
    exit_training(); select_stage(18)
    run('display_mode_test.py', '--minimum-side-pixels', 100)
    run('audio_capture_test.py')
    run('qol_live_test.py')
    run('verify_live_image.py')
    for name in ('display-mode-test.json','audio-capture-test.json','qol-live-test.json','live-image-verification.json'):
        (OUTPUT/name).write_bytes((ROOT/'build'/name).read_bytes())
    print('Update 5 texture, gameplay, display, audio, QoL and image checks passed', flush=True)


if __name__ == '__main__': main()
