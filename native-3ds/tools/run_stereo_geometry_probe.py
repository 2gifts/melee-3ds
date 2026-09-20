"""Run the isolated shader experiment in a separate portable emulator profile.

Does not attach to, stop or reconfigure the gameplay emulator. A timeout leaves
the owned probe process alive for inspection; --resume observes that same PID.
"""
import argparse, hashlib, json, os, re, shutil, subprocess, time
from pathlib import Path
import numpy as np
from PIL import Image
from build import ROOT

OUT=ROOT/'build/stereo-geometry-probe'
EMU=OUT/'emulator'
SD=EMU/'user/sdmc/stereo-probe'


def main():
    global OUT, EMU, SD
    ap=argparse.ArgumentParser();ap.add_argument('--resume',action='store_true');ap.add_argument('--run')
    ap.add_argument('--software-shaders',action='store_true',help='Run all vertex paths through CPU shader emulation for precision attribution')
    args=ap.parse_args()
    binary=OUT/'probe.3dsx';build_manifest=OUT/'build.json'
    if args.run:
        assert re.fullmatch(r'[a-z0-9-]+',args.run)
        OUT=OUT/args.run;EMU=OUT/'emulator';SD=EMU/'user/sdmc/stereo-probe'
    if not args.resume:
        source=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1'
        EMU.mkdir(parents=True,exist_ok=True)
        (EMU/'user/config').mkdir(parents=True,exist_ok=True)
        assert not (OUT/'process.json').exists(), 'Existing run: inspect it or use --resume'
        shutil.copyfile(source/'azahar.exe',EMU/'azahar.exe')
        shutil.copyfile(binary,OUT/'tested.3dsx')
        shutil.copyfile(build_manifest,OUT/'tested-build.json')
        (EMU/'qt.conf').write_text('[Paths]\nPrefix = '+source.as_posix()+'\n')
        config=(source/'user/config/qt-config.ini').read_text()
        for name,value in [('use_gdbstub','false'),('gdbstub_port','24690'),*([('use_hw_shader','false')] if args.software_shaders else [])]:
            config=re.sub(r'^'+name+r'=.*$',name+'='+value,config,flags=re.M)
            config=re.sub(r'^'+name+r'\\default=.*$',name+r'\\default=false',config,flags=re.M)
        (EMU/'user/config/qt-config.ini').write_text(config)
        env=os.environ.copy();env['PATH']=str(source)+os.pathsep+env.get('PATH','')
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
        with (OUT/'emulator-console.log').open('w') as log:
            p=subprocess.Popen([str(EMU/'azahar.exe'),str(OUT/'tested.3dsx')],cwd=EMU,env=env,startupinfo=si,stdout=log,stderr=subprocess.STDOUT)
        (OUT/'process.json').write_text(json.dumps(dict(pid=p.pid,exe=str(EMU/'azahar.exe'),started=time.time(),binary_sha256=hashlib.sha256((OUT/'tested.3dsx').read_bytes()).hexdigest()),indent=2)+'\n')
        print('Started isolated probe PID',p.pid,flush=True)
    else:
        info=json.loads((OUT/'process.json').read_text());print('Resuming observation of PID',info['pid'],flush=True)
    deadline=time.monotonic()+45
    while True:
        try:
            report=json.loads((SD/'report.json').read_text())
            break
        except (FileNotFoundError,json.JSONDecodeError):pass
        if (SD/'error.txt').exists():raise RuntimeError((SD/'error.txt').read_text())
        if time.monotonic()>deadline:raise TimeoutError('Probe report pending; inspect recorded PID and resume without restarting')
        time.sleep(.25)
    tested_build=json.loads((OUT/'tested-build.json').read_text())
    fixtures=tested_build.get('fixtures',10)
    modes=tested_build.get('modes',2)
    assert report['completed']==modes*fixtures,report
    comparisons=[]
    for fixture in range(fixtures):
        data=[np.fromfile(SD/f'fixture-{fixture}-{mode}.bin',dtype=np.uint8) for mode in range(modes)]
        assert all(a.size==240*800*4 for a in data)
        changed=np.abs(data[0].astype(np.int16)-data[1].astype(np.int16))
        foreground=[int(np.count_nonzero(a.reshape(-1,4)[:,1:])) for a in data]
        assert min(foreground)>100,'Empty scene cannot validate stereo geometry'
        # Display transfer is native-column-major RGBA8 byte order ABGR.
        if not tested_build.get('stress') or fixture in (0,5,63,64,127):
            for mode,a in enumerate(data):
                rgb=np.rot90(a.reshape(800,240,4)[:,:,1:][:,:,::-1])
                Image.fromarray(rgb).save(OUT/f'fixture-{fixture}-{mode}.png')
        comparisons.append(dict(fixture=fixture,changed_channels=int(np.count_nonzero(changed)),
            maximum_error=int(changed.max()),foreground_channels=foreground,
            scene=fixture%report.get('scenes',10),clipping_implemented=tested_build['clipping_implemented'],
            routes=report.get('routes',[])[fixture] if report.get('routes') else None,
            command_bytes=report.get('command_bytes',[])[fixture] if report.get('command_bytes') else None))
        if modes==3:
            for a,b,label in ((0,2,'original_vs_viewport'),(1,2,'reuse_vs_viewport')):
                delta=np.abs(data[a].astype(np.int16)-data[b].astype(np.int16))
                comparisons[-1][label]=dict(changed_channels=int(np.count_nonzero(delta)),maximum_error=int(delta.max()))
            comparisons[-1]['same_error_locations']=bool(np.array_equal(data[0]!=data[1],data[0]!=data[2]))
    result=dict(comparisons=comparisons,experimental=True,integrated_into_game=False,
                exact_fixtures=[c['fixture'] for c in comparisons if not c['changed_channels']],
                mismatched_fixtures=[c['fixture'] for c in comparisons if c['changed_channels']],
                accepted_for_production=False,
                physical_fps_verified=False,scope='Isolated GPU output; no gameplay performance claim')
    if 'bounds_cpu_ticks' in report:
        result['bounds_cpu_benchmark']={k:report[k] for k in ('bounds_cpu_ticks','bounds_calls','bounds_checksum')}
        result['bounds_cpu_benchmark']['optimized_over_reference']=report['bounds_cpu_ticks'][1]/report['bounds_cpu_ticks'][0]
    if tested_build.get('early_queue'):
        assert report['early_queue_starts']==report['early_queue_finishes']==fixtures,report
        result.update(experiment='Early command prefix, identical ordinary stereo shader',
                      early_queue_starts=report['early_queue_starts'],early_queue_finishes=report['early_queue_finishes'])
    if modes==3:
        result['viewport_control_summary']={label:dict(changed_channels=sum(c[label]['changed_channels'] for c in comparisons),maximum_error=max(c[label]['maximum_error'] for c in comparisons)) for label in ('original_vs_viewport','reuse_vs_viewport')}
        result['original_vs_reuse_summary']=dict(changed_channels=sum(c['changed_channels'] for c in comparisons),maximum_error=max(c['maximum_error'] for c in comparisons))
        result['same_error_locations']=all(c['same_error_locations'] for c in comparisons)
    (OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True)


if __name__=='__main__': main()
