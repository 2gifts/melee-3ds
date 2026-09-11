"""Exercise the increased geometry capacity over full stage/character rotation."""
import json, subprocess, sys
from gameplay_test import ROOT
from profile_render_detail import snapshot

OUTPUT = ROOT/'build/update6-qa'


def run(tool, *args, label=None):
    name = label or tool.removesuffix('.py')
    with (OUTPUT/(name+'.log')).open('w') as log:
        subprocess.run([sys.executable, str(ROOT/'tools'/tool), *map(str,args)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print('Passed '+name, flush=True)


def main():
    OUTPUT.mkdir(exist_ok=True)
    run('select_test_stage.py', 11, '--character', 22, '--cpu', 16, '--fresh')
    initial = snapshot(False)
    assert initial['memory']['geometry_budget'] == 12*1024*1024, initial
    run('geometry_cache_test.py', '--seconds', 5)
    (OUTPUT/'geometry-cache-test.json').write_bytes((ROOT/'build/geometry-cache-test.json').read_bytes())
    run('profile_render_detail.py', '--seconds', 6)
    (OUTPUT/'venom-profile.json').write_bytes((ROOT/'build/render-detail-profile.json').read_bytes())
    run('stage_sweep.py', '--stages', ','.join(map(str,range(29))), '--character-offset', 0,
        '--label', 'update6-all-stage', '--image-directory', 'update6-stage-sweep')
    stages = json.loads((ROOT/'build/update6-all-stage.json').read_text())
    assert {s['stage_index'] for s in stages} == set(range(29))
    assert {s['character_icon'] for s in stages} == set(range(25))
    for stage in stages:
        memory = stage['profile']['final_memory']
        assert memory['geometry_bytes'] <= 12*1024*1024, stage
        assert memory['mp_native_heap_available'] >= 8*1024*1024, stage
    run('verify_live_image.py')
    (OUTPUT/'live-image-verification.json').write_bytes((ROOT/'build/live-image-verification.json').read_bytes())
    print('Update 6 all-stage, all-character-slot, cache and image checks passed', flush=True)


if __name__ == '__main__': main()
