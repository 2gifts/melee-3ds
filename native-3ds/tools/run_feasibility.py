"""Read a bounded native measurement without debugger traffic during timing."""
import argparse,hashlib,json,struct,time
from pathlib import Path
from gameplay_test import ROOT,TEST_ELF,symbols
from results_capture_scene_test import connection
from efb_copy_test import SD
from run_render_benchmark import emulator_evidence,live_process

def measure(out,frames=180,profile=1,mode=0,links=0xffffffff):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    process=json.loads((TEST_ELF.parent/'process.json').read_text())
    assert Path(process['elf']).resolve()==TEST_ELF.resolve() and process['elf_sha256']==hashlib.sha256(TEST_ELF.read_bytes()).hexdigest()
    assert live_process(process['pid'])
    request=(time.time_ns()&0x7fffffff) or 1
    metadata=dict(id=request,frames=frames,profile=profile,mode=mode,links=links,process=process,
        emulator=emulator_evidence(process,out,'feasibility'),physical_throughput_verified=False)
    with connection() as (get,put):
        word=lambda n:int.from_bytes(get(symbols[n],4),'big')
        assert word('mp_probe_done')==word('mp_probe_request'),'Another measurement owns the profiler'
        metadata['simulation_before']=word('gm_80479D58')
        for name,value in [('mp_probe_frames',frames),('mp_probe_profile',profile),('mp_probe_links',links),('mp_probe_mode',mode),('mp_probe_request',request)]:put(symbols[name],value.to_bytes(4,'big'))
    (out/'request.json').write_text(json.dumps(metadata,indent=2))
    print('Native measurement requested',request,'frames',frames,'mode',mode,flush=True)
    deadline=time.monotonic()+240
    while True:
        assert live_process(process['pid']),'Owned process exited'
        log=(SD/'game.log').read_text(errors='replace')
        if f'Feasibility measurement complete id={request} ' in log:break
        if time.monotonic()>deadline:raise TimeoutError('Measurement remains owned; inspect/resume without restarting the emulator')
        time.sleep(.5)
    rows=[];reverse={v:k for k,v in symbols.items()}
    with connection() as (get,put):
        raw=get(symbols['mp_probe_result'],32);header=struct.unpack('>8I',raw)
        assert header[0]==request and not any(header[6:]),header
        data=b''.join(get(symbols['mp_probe_rows']+i,4096) for i in range(0,32768,4096))
        metadata['simulation_after']=int.from_bytes(get(symbols['gm_80479D58'],4),'big')
        metadata['engine_failed']=int.from_bytes(get(symbols['engine_failed'],4),'little')
        assert not metadata['engine_failed']
    for i in range(1024):
        domain,fn,context,calls,inclusive,own=struct.unpack_from('>4I2Q',data,i*32)
        if domain:rows.append(dict(domain=domain,function=reverse.get(fn,hex(fn)),address=fn,context=context,calls=calls,
            inclusive_ms=inclusive/40500,own_ms=own/40500,inclusive_ms_per_render=inclusive/40500/frames,own_ms_per_render=own/40500/frames))
    ticks=(header[2]<<32)|header[3]
    report=dict(**metadata,ticks=ticks,mean_frame_ms=ticks/40500/frames,render_fps=frames*40500000/ticks,
        surrounding_simulation_updates=metadata['simulation_after']-metadata['simulation_before'],
        simulation_count_scope='Debugger snapshots surround the native window; use domain-4 calls for measured simulation-loop iterations',
        rows=sorted(rows,key=lambda x:x['own_ms'],reverse=True))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('rows','process','emulator')}),flush=True)
    print(json.dumps(report['rows'][:12],indent=2),flush=True)
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);ap.add_argument('--frames',type=int,default=180)
    ap.add_argument('--no-profile',action='store_true');a=ap.parse_args()
    measure(ROOT/a.output,a.frames,int(not a.no_profile))
