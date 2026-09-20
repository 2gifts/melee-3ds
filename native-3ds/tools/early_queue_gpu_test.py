"""Exercise early submission through the game's actual renderer and fixtures."""
import argparse,hashlib,importlib,json,sys,time
from pathlib import Path
from gameplay_test import ROOT,TEST_ELF
from profile_switch import set_word
from texture_visibility_test import read


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,default=ROOT/'build/early-queue-qa/fixtures');ap.add_argument('--stream',action='store_true')
    ap.add_argument('--async-presentation',action='store_true');args=ap.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    names=('gpu_early_queue_disable','gpu_early_queue_bytes','stereo_reuse_disable','mp_test_stereo_slider')
    if args.stream:names+=('gpu_stream_queue_disable',)
    if args.async_presentation:names+=('gpu_async_present_disable',)
    original=read(names);rows=[];passed=False
    def counters():return read(('mp_early_queue_starts','mp_early_queue_finishes','engine_failed')+
        (('mp_early_queue_deferrals','mp_early_queue_retirements','mp_queue_publications','mp_queue_completions') if args.async_presentation else ()))
    try:
        if args.stream:set_word('gpu_stream_queue_disable',0)
        if args.async_presentation:set_word('gpu_async_present_disable',0)
        set_word('stereo_reuse_disable',1);set_word('gpu_early_queue_bytes',256)
        set_word('mp_test_stereo_slider',1000);set_word('gpu_early_queue_disable',0)
        tests=[('shader_gpu_test',['--flags','1']),('point_gpu_test',['--stereo']),('stereo_gpu_test',[]),
               ('shield_gpu_test',None),('layered_gpu_test',None),('raster_state_gpu_test',None)]
        for name,options in tests:
            module=importlib.import_module(name);case=out/name;case.mkdir(exist_ok=True)
            before=counters();started=time.monotonic()
            if options is None:
                # These older tools use ROOT only for report destinations;
                # their captured SD path and symbol map are already bound.
                (case/'build').mkdir(exist_ok=True);module.ROOT=case
                sys.argv=[name]
            else:sys.argv=[name,*options,'--output',str(case)]
            module.main()
            after=counters();assert not after['engine_failed'],after
            starts=after['mp_early_queue_starts']-before['mp_early_queue_starts']
            assert starts>=2,(name,'Fixture did not exercise early submission',before,after)
            if args.async_presentation:
                assert after['mp_early_queue_deferrals']>before['mp_early_queue_deferrals'],(name,before,after)
                assert after['mp_early_queue_retirements']>before['mp_early_queue_retirements'],(name,before,after)
            rows.append(dict(test=name,passed=True,early_starts=starts,before=before,after=after,host_seconds=time.monotonic()-started))
            (out/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
            print('PASS early queue:',name,'prefixes:',starts,flush=True)
        passed=True
    finally:
        for name,value in original.items():set_word(name,value)
        (out/'result.json').write_text(json.dumps(dict(passed=passed,tests=rows,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
            streaming=args.stream,async_presentation=args.async_presentation,physical_console_tested=False,scope='GPU correctness with forced early submission; not gameplay FPS'),indent=2)+'\n')


if __name__=='__main__':main()
