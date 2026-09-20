"""Start the isolated development ELF; the original build remains archived."""
import argparse,hashlib,json,os,re,socket,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=Path(os.environ.get('MP_TEST_ELF',ROOT/'build/game-stereo-reuse/melee.elf')).resolve().parent
os.environ['MP_TEST_ELF']=str(OUT/'melee.elf')
from gameplay_test import symbols
import select_test_stage as select
from profile_switch import set_word


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--software-shaders',action='store_true',help='Compare both rendering paths without host vertex-shader acceleration')
    ap.add_argument('--render-worker-async',action='store_true',help='Exercise queued native rendering immediately after boot control becomes available')
    ap.add_argument('--render-worker-borrow',action='store_true',help='Borrow immutable engine geometry through the versioned retirement contract')
    args=ap.parse_args()
    if args.render_worker_borrow and not args.render_worker_async:ap.error('--render-worker-borrow requires --render-worker-async')
    # Never dispatch a duplicate game while the expected GDB port is live.
    try:
        with socket.create_connection(('127.0.0.1',24689),.2):pass
    except OSError:pass
    else:raise RuntimeError('A GDB server is already live on port 24689')
    si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
    exe=ROOT/'.toolchain/azahar/azahar-windows-msys2-2126.1/azahar.exe'
    if args.software_shaders:
        config=exe.parent/'user/config/qt-config.ini';backup=OUT/'qt-config-before-software-shaders.ini'
        assert not backup.exists(),'Existing configuration backup: inspect the prior run before launching again'
        raw=config.read_bytes();backup.write_bytes(raw)
        text=raw.decode('utf-8')
        for key in ('use_hw_shader','use_hw_shader\\default'):
            text,n=re.subn(r'^'+re.escape(key)+r'=.*$',lambda _:key+'=false',text,flags=re.M)
            assert n==1,('Missing or duplicate shader setting',key,n)
        config.write_text(text,encoding='utf-8')
    p=subprocess.Popen([str(exe),str(OUT/'melee-development.3dsx')],startupinfo=si)
    (OUT/'process.json').write_text(json.dumps(dict(pid=p.pid,exe=str(exe),elf=str(OUT/'melee.elf'),
        elf_sha256=hashlib.sha256((OUT/'melee.elf').read_bytes()).hexdigest(),software_shaders_requested=args.software_shaders),indent=2)+'\n')
    print('Started stereo development game PID',p.pid,flush=True)
    for _ in range(150):
        assert p.poll() is None,'Emulator exited during boot'
        try:
            with socket.create_connection(('127.0.0.1',24689),.2):pass
            break
        except OSError:time.sleep(.1)
    else:raise TimeoutError('GDB is pending; emulator remains alive for inspection')
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    if args.render_worker_async:
        assert 'mp_render_worker_async' in symbols
        set_word('mp_render_worker_async',1)
    for _ in range(100):
        state=select.progress();assert not state['failed'],state
        if state['frame']>60:break
        time.sleep(.25)
    else:raise TimeoutError('Boot pending; emulator remains alive for inspection')
    if args.render_worker_borrow:
        from texture_visibility_test import read
        assert read(('mp_render_worker_geometry_contract',))['mp_render_worker_geometry_contract']==1
        set_word('mp_render_worker_geometry_borrow',1)
    assert 'stereo_reuse_disable' in symbols
    (OUT/'startup.json').write_text(json.dumps(state,indent=2)+'\n');print('Ready',state,flush=True)


if __name__=='__main__':main()
