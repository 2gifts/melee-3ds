"""Measure renderer modes without any debugger traffic inside the windows.

Requires an original paused match. A timeout leaves the owned native run
alone; resume observation with the same output directory and --resume.
"""
import argparse,ctypes,hashlib,json,socket,struct,time
from pathlib import Path
import numpy as np
from gameplay_test import ROOT,TEST_ELF,symbols,packet,receive
from efb_copy_test import SD
from profile_switch import set_word
from texture_visibility_test import read
from menu_display_pause_test import clock_state
import bottom_screen_test as bottom


def live_process(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong];kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];kernel.WaitForSingleObject.restype=ctypes.c_ulong
    kernel.CloseHandle.argtypes=[ctypes.c_void_p];kernel.CloseHandle.restype=ctypes.c_int
    handle=kernel.OpenProcess(0x100000,False,pid)
    if not handle:raise OSError(ctypes.get_last_error(),'Cannot inspect benchmark process')
    try:return kernel.WaitForSingleObject(handle,0)==258
    finally:kernel.CloseHandle(handle)


def emulator_evidence(process,out,experiment):
    exe=Path(process['exe'])
    log=(exe.parent/'user/log/azahar_log.txt').read_text(encoding='utf-8',errors='replace')
    lines=[line for line in log.splitlines() if 'Azahar Version:' in line or 'Config <Info>' in line]
    assert any('Azahar Version:' in line for line in lines),'Missing emulator startup provenance'
    (out/'emulator-startup.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    hw=next(line.rsplit(': ',1)[1] for line in lines if 'Renderer_UseHwShader:' in line)
    api=next(line.rsplit(': ',1)[1] for line in lines if 'Renderer_GraphicsAPI:' in line)
    def setting(name):return next(line.rsplit(': ',1)[1] for line in lines if name+':' in line)
    result=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),graphics_api=api,use_hw_shader=hw=='true',
        cpu_clock_percentage=int(setting('Core_CPUClockPercentage')),
        frame_limit_percentage=int(setting('Renderer_FrameLimit')),
        simulate_3ds_gpu_timings=setting('Renderer_Simulate3DSGPUTimings')=='true',
        physical_gpu_throughput_verified=False)
    if experiment=='stereo_reuse':
        result.update(geometry_shader_acceleration=False,
            reason='Azahar PicaCore::DrawArrays bypasses accelerated draws when a geometry shader is enabled. '
                   'Host frame/queue timings across these paths cannot prove New 3DS GPU throughput.')
    elif experiment.startswith('render_rework'):
        result.update(shader_path_held_constant=False,
            reason='The clamped material candidate deliberately moves eligible CPU vertices to hardware shaders. '
                   'Host GPU completion timing does not establish physical New 3DS throughput; '
                   'this benchmark measures the complete emulator path, not a hardware FPS prediction.')
    else:
        result.update(shader_path_held_constant=True,
            reason='Both modes use the same renderer shaders, but host GPU completion timing does not '
                   'establish physical New 3DS throughput. Pacing limits can hide a difference.')
    return result


def camera_state():
    # Versus pause uses x2C4/x2C5; the alternate photo camera uses x304/x305.
    # Both share pan/eye/distance. Record both rather than misidentifying the
    # inactive photo-camera slot as the fighter currently in view.
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            packet(sock,f'm{symbols["game_camera"]+0x2c4:x},70');raw=bytes.fromhex(receive(sock));assert len(raw)==112
            return dict(pause_focused_slot=raw[0],pause_controller=raw[1],photo_focused_slot=raw[64],photo_controller=raw[65],
                        pan=list(struct.unpack('>3f',raw[80:92])),eye_offset=list(struct.unpack('>3f',raw[92:104])),
                        distance=struct.unpack('>f',raw[108:112])[0],distance_limits=list(struct.unpack('>2f',raw[52:60])))
        finally:packet(sock,'c');packet(sock,'D');receive(sock)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--frames',type=int,default=180);ap.add_argument('--resume',action='store_true')
    experiments=('stereo_reuse','early_queue','stream_queue','async_presentation','clamped_shade','unlit_affine','render_worker','render_rework','render_rework_component')
    ap.add_argument('--experiment',choices=experiments,default='stereo_reuse');ap.add_argument('--rework-mask',type=int,default=7);args=ap.parse_args()
    out=args.output;out.mkdir(parents=True,exist_ok=True);metadata=out/'request.json'
    if args.experiment.startswith('render_rework') and not args.resume:
        set_word('mp_render_bench_rework_mask',args.rework_mask)
        for name in ('mp_gx_profile','geometry_compare_validate','geometry_validate','geometry_early_validate','geometry_planes_validate'):set_word(name,0,'big')
    if args.resume:request=json.loads(metadata.read_text())
    else:
        assert not metadata.exists(),'Existing request: inspect it or use --resume'
        assert 60<=args.frames<=600
        process=json.loads((TEST_ELF.parent/'process.json').read_text())
        assert Path(process['elf']).resolve()==TEST_ELF.resolve()
        assert process['elf_sha256']==hashlib.sha256(TEST_ELF.read_bytes()).hexdigest()
        assert live_process(process['pid'])
        assert read(('mp_render_bench_status',))['mp_render_bench_status']!=2,'A native benchmark is already running'
        initial=clock_state();assert initial['pause_flags'],'Pause through original controller input first'
        request=dict(id=(time.time_ns()&0x7fffffff) or 1,initial_clock=initial,initial_state=bottom.snapshot(),
            process=process,frames=args.frames,physical_fps_verified=False,emulator=emulator_evidence(process,out,args.experiment),camera=camera_state(),experiment=args.experiment,rework_mask=args.rework_mask if args.experiment.startswith('render_rework') else None)
        if args.experiment=='async_presentation':assert 'gpu_async_present_disable' in symbols
        if args.experiment=='clamped_shade':assert 'shade_clamped_disable' in symbols and 'gpu_async_present_disable' in symbols
        if args.experiment=='unlit_affine':assert all(s in symbols for s in ('unlit_affine_disable','shade_clamped_disable','gpu_async_present_disable'))
        if args.experiment=='render_worker':
            from render_worker_evidence import processor_evidence
            assert all(s in symbols for s in ('mp_render_worker_async','mp_render_worker_snapshots'))
            request['worker_processor']=processor_evidence(read(('mp_render_worker_active','mp_render_worker_core')))
            if 'mp_render_worker_geometry_borrow' in symbols:
                request['geometry_borrow']=read(('mp_render_worker_geometry_borrow','mp_render_worker_geometry_contract'))
        if 'mp_render_bench_experiment' in symbols:set_word('mp_render_bench_experiment',experiments.index(args.experiment))
        else:assert args.experiment=='stereo_reuse','This ELF has no early-queue benchmark'
        set_word('mp_gx_profile',0,'big');set_word('mp_test_frame_limit',0);set_word('mp_test_stereo_slider',1000)
        for name in ('uniform_dispatch_disable','shader_shortcuts_disable','uniform_scope_disable','lighting_uniform_disable',
                     'uniform_dispatch_validate','stereo_reuse_bounds_reference'):
            if name in symbols:set_word(name,0)
        set_word('mp_render_bench_frames',args.frames)
        metadata.write_text(json.dumps(request,indent=2)+'\n')
        set_word('mp_render_bench_request',request['id'])
        print('Requested native benchmark',request['id'],'in PID',process['pid'],flush=True)
    deadline=time.monotonic()+300;last=-1
    while True:
        # Read filesystem output only: no GDB connections during native timing.
        if not live_process(request['process']['pid']):raise RuntimeError('Owned emulator process exited')
        try:result=json.loads((SD/'render-benchmark.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):result={}
        if result.get('request')==request['id'] and result.get('complete'):break
        try:progress=json.loads((SD/'render-benchmark-progress.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):progress={}
        if progress.get('request')==request['id'] and progress['completed_samples']!=last:
            last=progress['completed_samples'];print('Completed native samples',last,'of 6',flush=True)
        if time.monotonic()>deadline:raise TimeoutError('Native run remains owned; inspect its PID and resume observation without restarting')
        time.sleep(.5)
    (out/'native.json').write_text(json.dumps(result,indent=2)+'\n')
    final=clock_state();assert final==request['initial_clock'],(request['initial_clock'],final)
    if 'camera' in request:assert camera_state()==request['camera'],'Pause camera changed during the benchmark'
    assert result['same_bottom_state'] and result['stereo_slider']==1000,result
    unlit=request.get('experiment')=='unlit_affine'
    worker=request.get('experiment')=='render_worker'
    rework=request.get('experiment','').startswith('render_rework')
    component=request.get('experiment')=='render_rework_component'
    assert [s['mode'] for s in result['samples']]==([0,1,1,0,0,1] if unlit or worker or component else [0,1,2,2,1,0])
    stream=request.get('experiment')=='stream_queue'
    async_present=request.get('experiment')=='async_presentation'
    clamped=request.get('experiment')=='clamped_shade'
    early=request.get('experiment') in ('early_queue','stream_queue','async_presentation','clamped_shade','unlit_affine','render_worker','render_rework','render_rework_component')
    assert result.get('experiment',0)==experiments.index(request.get('experiment','stereo_reuse'))
    frequency=result['tick_frequency'];summaries=[]
    for s in result['samples']:
        n=s['frames'];assert n==request['frames']==len(s['frame_ticks'])==s['end_frame']-s['start_frame']
        assert min(s['frame_ticks'])>0 and s['ticks']>=sum(s['frame_ticks'])
        assert s['command_errors']==0 and s['command_bytes']>0 and s['draw_routes'][0]>0
        if early:
            assert s['draw_routes'][1]==0
            assert s['early_starts']==s['early_finishes']
            if rework:
                assert s['early_starts']>=n and s['async_deferrals']==s['async_retirements']>=n
                assert not s['worker_failures'] and s['worker_snapshots']>n
            elif worker:
                assert s['early_starts']>=n and s['async_deferrals']==s['async_retirements']>=n
                assert not s['worker_failures']
                assert s['worker_snapshots']==0 if s['mode']==0 else s['worker_snapshots']>n
            elif unlit:
                assert s['early_starts']>=n and s['async_deferrals']==s['async_retirements']>=n
                assert s['unlit_vertices'][1]==0 if s['mode']==0 else s['unlit_vertices'][1]>n
            elif clamped:
                assert s['early_starts']>=n
                assert s['async_deferrals']==s['async_retirements']
                assert s['async_deferrals']==0 if s['mode']!=2 else s['async_deferrals']>=n
                assert s['clamped_vertices']==0 if s['mode']==0 else s['clamped_vertices']>n
            elif async_present:
                assert s['early_starts']>=n
                assert s['async_deferrals']==s['async_retirements']
                assert s['async_deferrals']==0 if s['mode']==0 else s['async_deferrals']>=n
            elif stream:assert s['early_starts']==n if s['mode']==0 else s['early_starts']>n
            else:assert s['early_starts']==0 if s['mode']==0 else s['early_starts']>=n
        else:assert (s['draw_routes'][1]==0) if s['mode']==0 else (s['draw_routes'][1]>0)
        durations=np.array(s['frame_ticks'],dtype=np.float64)*1000/frequency
        summaries.append(dict(mode=s['mode'],frames=n,emulated_fps=n*frequency/s['ticks'],
            frame_ms_percentiles=dict(zip(('minimum','p50','p95','p99','maximum'),np.percentile(durations,[0,50,95,99,100]).tolist())),
            bounds_ms_per_frame=s['bounds_ticks']/40500/n,command_bytes_per_frame=s['command_bytes']/n,
            gpu_wait_ms_per_frame=s['gpu_wait_ticks']*1000/frequency/n,
            completed_gpu_queue_ms=s['gpu_ms']/s['gpu_queues'] if s['gpu_queues'] else None,
            draw_routes_per_frame=[v/n for v in s['draw_routes']],vertex_routes_per_frame=[v/n for v in s['vertex_routes']]))
        if 'shader_vertices' in s:
            shader=s['shader_vertices'];assert sum(shader)==sum(s['vertex_routes']) and shader[2]>=s['vertex_routes'][1]
            summaries[-1].update(shader_vertices_per_frame=[v/n for v in shader],
                reuse_fraction_of_lit_vertices=s['vertex_routes'][1]/shader[2] if shader[2] else 0)
        if early:
            summaries[-1].update(early_prefixes_per_frame=s['early_starts']/n,
                early_wait_ms_per_frame=s['early_wait_ticks']*1000/frequency/n,
                early_span_ms_per_frame=s['early_span_ticks']*1000/frequency/n,
                sdk_gpu_timing_scope='Tail starts with any undrained prefixes in async mode; cannot be compared as whole-frame GPU time')
        if async_present:summaries[-1].update(async_deferrals=s['async_deferrals'],async_retirements=s['async_retirements'])
        if clamped:summaries[-1].update(clamped_vertices_per_frame=s['clamped_vertices']/n,
            mode_description=('update18_wait_scalar_materials','update18_wait_compiled_materials','deferred_wait_compiled_materials')[s['mode']])
        if unlit:summaries[-1].update(unlit_vertices_per_frame=[x/n for x in s['unlit_vertices']],unlit_draws_per_frame=[x/n for x in s['unlit_draws']])
        if worker or rework:
            summaries[-1].update({k+'_per_frame':s[k]/n for k in ('worker_jobs','worker_snapshots','worker_fallbacks','worker_flushes')})
            summaries[-1].update({k.replace('_ticks','_ms_per_frame'):s[k]*1000/frequency/n for k in ('worker_wait_ticks','worker_busy_ticks','worker_copy_ticks')})
            if 'source_copied_bytes' in s:
                assert s['source_checks']==s['source_mismatches']==0,'Source validation must be disabled during timing'
                if s['mode']:assert s['source_hits']>n,'Persistent texture reuse was not exercised'
                summaries[-1].update({k+'_per_frame':s[k]/n for k in ('source_copied_bytes','source_hit_bytes','source_hits')})
            if 'borrowed_draws' in s:
                if s['mode'] and request.get('geometry_borrow',{}).get('mp_render_worker_geometry_borrow'):
                    assert s['borrowed_draws']>n and s['borrowed_bytes']>10000*n,'Shared geometry was not exercised'
                summaries[-1].update({k+'_per_frame':s[k]/n for k in ('borrowed_draws','borrowed_bytes')})
    report=dict(measurement_valid=True,request=request['id'],samples=summaries,final_clock=final,
        elf_sha256=request['process']['elf_sha256'],physical_fps_verified=False,accepted_for_release=False,
        emulator=request.get('emulator'),experiment=request.get('experiment','stereo_reuse'),
        scope='Uninterrupted emulator windows of an original paused encounter; gameplay performance targets remain unverified')
    (out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    for s in summaries:print(json.dumps(s),flush=True)


if __name__=='__main__':main()
