"""Keep early submission active while existing resource-fence tests run."""
import argparse,hashlib,json,sys
from pathlib import Path
from gameplay_test import ROOT,TEST_ELF
from profile_switch import set_word
from texture_visibility_test import read
import command_buffer_test as pressure

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--stream',action='store_true')
    ap.add_argument('--async-presentation',action='store_true');args=ap.parse_args()
    out=args.output.resolve();(out/'build').mkdir(parents=True,exist_ok=True)
    flags=('gpu_early_queue_disable','gpu_early_queue_bytes','stereo_reuse_disable','mp_test_stereo_slider')
    if args.stream:flags+=('gpu_stream_queue_disable',)
    if args.async_presentation:flags+=('gpu_async_present_disable',)
    counters=('mp_early_queue_starts','mp_early_queue_finishes','command_barriers','stream_barriers','engine_failed')
    if args.stream:counters+=('mp_early_queue_appends','mp_early_queue_runs','mp_early_queue_capacity')
    if args.async_presentation:counters+=('mp_early_queue_deferrals','mp_early_queue_retirements','mp_queue_publications','mp_queue_completions','mp_queue_capacity_drains')
    original=read(flags);before=read(counters);passed=False
    try:
        if args.stream:set_word('gpu_stream_queue_disable',0)
        if args.async_presentation:set_word('gpu_async_present_disable',0)
        set_word('stereo_reuse_disable',1);set_word('mp_test_stereo_slider',1000)
        set_word('gpu_early_queue_bytes',256);set_word('gpu_early_queue_disable',0)
        for module in (pressure,pressure.point_gpu_test,pressure.cull_gpu_test,pressure.blend_gpu_test,
                       pressure.efb_copy_test,pressure.raster_state_gpu_test):module.ROOT=out
        sys.argv=['command_buffer_test.py'];pressure.main()
        after=read(counters)
        assert not after['engine_failed']
        for name in ('mp_early_queue_starts','mp_early_queue_finishes','command_barriers','stream_barriers'):
            assert after[name]>before[name],(name,before,after)
        if args.stream:assert after['mp_early_queue_appends']>before['mp_early_queue_appends']
        if args.async_presentation:
            assert after['mp_early_queue_deferrals']>before['mp_early_queue_deferrals']
            assert after['mp_early_queue_retirements']>before['mp_early_queue_retirements']
        passed=True
    finally:
        cleanup_errors=[]
        for name,value in original.items():
            try:set_word(name,value)
            except (OSError,ValueError) as error:cleanup_errors.append(f'{name}: {error}');break
        try:after=read(counters)
        except (OSError,ValueError) as error:after=None;cleanup_errors.append(str(error))
        if cleanup_errors:passed=False
        (out/'result.json').write_text(json.dumps(dict(passed=passed,before=before,after=after,
            cleanup_errors=cleanup_errors,
            elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),physical_console_tested=False,streaming=args.stream,async_presentation=args.async_presentation,
            scope='Command and vertex arena recycling, framebuffer copies and pixel fixtures with early submission'),indent=2)+'\n')
    print('PASS: early GPU prefix with command and streaming resource pressure',flush=True)

if __name__=='__main__':main()
