"""Finish the isolated light-cache run after the original encounter is paused.

Run casual_scenario_test with cache enabled and validation enabled first.
This records existing live counters, then measures cache-only A/B with update
18 streaming held constant. It does not write the SD card or change game data.
"""
import hashlib,json,subprocess,sys
from pathlib import Path
from gameplay_test import ROOT,TEST_ELF,symbols
from render_update16_test import counters
from menu_display_pause_test import clock_state
from bottom_screen_test import snapshot


def main():
    out=ROOT/'build/light-cache-qa'
    process=json.loads((TEST_ELF.parent/'process.json').read_text())
    digest=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest()
    assert process['elf_sha256']==digest
    assert 'gpu_light_cache_disable' in symbols
    active=json.loads((out/'active/temple-scenario.json').read_text())
    assert active['passed'] and active['experimental_stream_queue']
    assert active['simulation_frames']>600 and clock_state()['pause_flags']
    before=dict(counters=counters(),scene=snapshot(),elf_sha256=digest,
                validator_enabled_before_original_menu_scenario=True)
    assert not before['scene']['engine_failed']
    assert before['counters']['gpu_light_cache_checks'][0]>10000
    (out/'live-validation.json').write_text(json.dumps(before,indent=2)+'\n')
    subprocess.run([sys.executable,ROOT/'tools/render_update16_test.py',
        '--flags','gpu_light_cache_disable','--stream-queue',
        '--output','build/light-cache-qa/paired'],check=True)
    subprocess.run([sys.executable,ROOT/'tools/profile_lighting_mix.py',
        '--output',str(out/'lighting-mix.json')],check=True)
    result=json.loads((out/'paired/render-ab.json').read_text())
    profiles=result['profiles']
    samples={str(reference):[p['phases']['shader_setup']['estimated_ms_per_render']
        for p in profiles if p['reference']==reference] for reference in (True,False)}
    means={key:sum(values)/len(values) for key,values in samples.items()}
    summary=dict(passed=result['passed'],elf_sha256=digest,
        shader_setup_ms=dict(reference=means['True'],cache=means['False'],samples=samples),
        saved_shader_setup_ms=means['True']-means['False'],
        different_eye_channels=result['different_channels'],
        live_conversion_checks=before['counters']['gpu_light_cache_checks'][0],
        paused_conversion_checks=result['light_checks'],physical_fps_verified=False,
        scope='Fixed paused Temple view; active four-player all-items correctness coverage separately recorded')
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary),flush=True)


if __name__=='__main__':main()
