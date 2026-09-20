"""Build the in-progress native Melee engine integration for 3DS."""
import argparse
import subprocess
import sys
from pathlib import Path
from build import ROOT, prepare, local_clang, common_flags, run
from engine_build import compile_engine


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--smoke',action='store_true')
    ap.add_argument('--banner-capture',action='store_true',help='Include offline banner geometry export (development builds only)')
    ap.add_argument('--audio-hle',action='store_true',help='Exercise NDSP using Azahar HLE without DSP firmware (smoke builds only)')
    ap.add_argument('--release',action='store_true',help='Build a separate homebrew package with physical controls')
    ap.add_argument('--skip-engine',action='store_true')
    ap.add_argument('--sanitize',action='store_true',help='Stop with a source location on original-engine null accesses')
    ap.add_argument('--boot',action='store_true',help='Enter Melee original main and scene loop')
    ap.add_argument('--output',type=Path,help='Alternate 3DSX destination, preserving an existing hardware test package')
    ap.add_argument('--build-dir',type=Path,help='Separate development objects/ELF; requires --smoke')
    ap.add_argument('--engine-build-dir',type=Path,help='Isolated BE8 engine objects/archive; requires --smoke and --build-dir')
    ap.add_argument('--shader-dir',type=Path,help='Use separately generated candidate vertex shaders; requires --smoke and --build-dir')
    ap.add_argument('--identity-affine',action='store_true',help='Enable the matching development-only affine shader interface; references update 17')
    ap.add_argument('--unlit-affine',action='store_true',help='Development-only unlit material shortcut with no additional lighting-path instructions')
    ap.add_argument('--async-presentation',action='store_true',help='Isolated development queue publication and deferred final GPU wait')
    ap.add_argument('--clamped-shade',action='store_true',help='Isolated CPU material compiler; requires a separate engine build directory')
    ap.add_argument('--engine-lto',action='store_true',help='Experimental ThinLTO inside an isolated BE8 engine, before conversion to native linkage')
    ap.add_argument('--engine-lto-scope',choices=('all','support'),default='all',help='Restrict experimental LTO to support services; keep Melee gameplay modules ordinary')
    ap.add_argument('--render-worker',action='store_true',help='Use immutable draw packets on the New 3DS core-2 renderer, with synchronous fallback')
    ap.add_argument('--material-program',action='store_true',help='Isolated shared material program/binding cache experiment')
    ap.add_argument('--feasibility',action='store_true',help='Isolated callback/graphics measurement build; never a release')
    ap.add_argument('--feasibility-console',action='store_true',help='Separate physical-controls measurement build with sparse automatic logs; no rendering omissions')
    ap.add_argument('--render-rework',action='store_true',help='Isolated renderer equivalence/performance controls')
    args=ap.parse_args()
    if args.render_rework and not (args.smoke and args.build_dir and args.engine_build_dir and args.render_worker):ap.error('--render-rework requires isolated smoke directories and --render-worker')
    if args.feasibility_console:
        if args.smoke or args.release or not (args.build_dir and args.engine_build_dir and args.output):ap.error('--feasibility-console requires separate directories/output and excludes smoke/release')
        if args.sanitize or args.skip_engine:ap.error('--feasibility-console must compile its own baseline engine without sanitizer changes')
        args.feasibility=True;args.boot=True;args.render_worker=True;args.async_presentation=True
    if args.feasibility and not ((args.smoke or args.feasibility_console) and args.build_dir and args.engine_build_dir):ap.error('--feasibility requires isolated directories')
    if args.banner_capture and not args.smoke:ap.error('--banner-capture requires --smoke')
    if args.audio_hle and not args.smoke:ap.error('--audio-hle requires --smoke; it cannot run on hardware')
    if args.build_dir and not (args.smoke or args.feasibility_console):ap.error('--build-dir requires --smoke')
    if args.engine_build_dir and not ((args.smoke or args.feasibility_console) and args.build_dir):ap.error('--engine-build-dir requires an isolated build')
    if args.shader_dir and not (args.smoke and args.build_dir):ap.error('--shader-dir requires --smoke and --build-dir')
    if args.identity_affine and not (args.smoke and args.shader_dir and args.build_dir):ap.error('--identity-affine requires an isolated smoke build and matching shader directory')
    if args.unlit_affine and not (args.smoke and args.shader_dir and args.build_dir and not args.identity_affine):ap.error('--unlit-affine requires isolated smoke/shader directories and excludes --identity-affine')
    if args.async_presentation and not ((args.smoke or args.feasibility_console) and args.build_dir):ap.error('--async-presentation requires an isolated build')
    if args.clamped_shade and not (args.smoke and args.build_dir and args.engine_build_dir):ap.error('--clamped-shade requires isolated smoke and engine directories')
    if args.engine_lto and not (args.smoke and args.build_dir and args.engine_build_dir):ap.error('--engine-lto requires isolated smoke and engine directories')
    if args.engine_lto_scope!='all' and not args.engine_lto:ap.error('--engine-lto-scope requires --engine-lto')
    if args.render_worker and not (args.release or ((args.smoke or args.feasibility_console) and args.build_dir)):ap.error('--render-worker requires a release or isolated build')
    if args.render_worker and (args.audio_hle or args.banner_capture):ap.error('--render-worker cannot run main-thread-only audio/banner export fixtures')
    if args.material_program and not (args.smoke and args.build_dir and args.engine_build_dir and args.clamped_shade):ap.error('--material-program requires isolated smoke and clamped-shade engine directories')
    if args.release:
        if args.smoke:ap.error('--release and --smoke are mutually exclusive')
        args.boot=True
        args.render_worker=True
    engine_out=args.engine_build_dir or ROOT/'build/engine-be8'
    if not engine_out.is_absolute():engine_out=ROOT/engine_out
    library=engine_out/'libmelee-be8.a'
    if not args.skip_engine: library=compile_engine(sanitize=args.sanitize,output_directory=engine_out,clamped_shade=args.clamped_shade,lto=args.engine_lto,lto_scope=args.engine_lto_scope,material_program=args.material_program,feasibility=args.feasibility,feasibility_console=args.feasibility_console,render_rework=args.render_rework)
    out=args.build_dir or ROOT/('build/game-release' if args.release else 'build/game')
    if not out.is_absolute():out=ROOT/out
    out.mkdir(parents=True,exist_ok=True)
    cc=local_clang();bin=Path(cc).parent
    sdk=ROOT/'.toolchain/devkitpro';arm=sdk/'devkitARM'
    arch=['--no-default-config','--target=arm-none-eabi','-mcpu=mpcore',
          '-mfpu=vfp','-mfloat-abi=hard','-mtp=soft']
    includes=[*prepare(),'-I'+str(sdk/'libctru/include'),'-isystem',str(arm/'arm-none-eabi/include')]
    shader=out/'vertex.shbin'
    shader_dir=args.shader_dir or ROOT/'port/3ds'
    if not shader_dir.is_absolute():shader_dir=ROOT/shader_dir
    for stem in ('vertex','vertex-dual','vertex-stereo','vertex-dual-stereo'):
        assert ('; MP_AFFINE_IDENTITY_SHADER' in (shader_dir/(stem+'.v.pica')).read_text())==args.identity_affine,'Affine shader interface mismatch'
        assert ('; MP_UNLIT_AFFINE_SHADER' in (shader_dir/(stem+'.v.pica')).read_text())==args.unlit_affine,'Unlit shader interface mismatch'
    run([ROOT/'.toolchain/bin/picasso.exe','-o',shader,shader_dir/'vertex.v.pica',ROOT/'port/3ds/point.g.pica'])
    shader_c=out/'shader.c'
    blob=shader.read_bytes()
    shader_c.write_text('const unsigned char mp_shader[] __attribute__((aligned(4)))={'+
                        ','.join(str(b) for b in blob)+'};\nconst unsigned mp_shader_size='+str(len(blob))+';\n')
    dual_shader=out/'vertex-dual.shbin'
    run([ROOT/'.toolchain/bin/picasso.exe','-o',dual_shader,shader_dir/'vertex-dual.v.pica'])
    dual_blob=dual_shader.read_bytes()
    with shader_c.open('a') as source:
        source.write('const unsigned char mp_dual_shader[] __attribute__((aligned(4)))={'+
                     ','.join(str(b) for b in dual_blob)+'};\nconst unsigned mp_dual_shader_size='+str(len(dual_blob))+';\n')
    # Clamped materials use separate programs: normal paths are byte-identical.
    for name,stem in [('base','vertex'),('dual','vertex-dual'),('stereo','vertex-stereo'),('dual_stereo','vertex-dual-stereo')]:
        binary=out/f'{stem}-clamped.shbin'
        sources=[ROOT/'port/3ds'/f'{stem}-clamped.v.pica']
        if name in ('base','stereo'):sources.append(ROOT/'port/3ds/point.g.pica')
        run([ROOT/'.toolchain/bin/picasso.exe','-o',binary,*sources])
        data=binary.read_bytes()
        with shader_c.open('a') as source:
            source.write(f'const unsigned char mp_clamped_{name}[] __attribute__((aligned(4)))={{'+','.join(str(b) for b in data)+f'}};\nconst unsigned mp_clamped_{name}_size={len(data)};\n')
    objects=[]
    if args.smoke:
        from stereo_reuse_shader import source as stereo_reuse_source
        reuse_source=out/'stereo-reuse.g.pica';reuse_source.write_text(stereo_reuse_source())
    for name in ('stereo','dual_stereo'):
        binary=out/f'vertex-{name}.shbin'
        sources=[shader_dir/f'vertex-{name.replace("_","-")}.v.pica']
        if name=='stereo':sources.append(ROOT/'port/3ds/point.g.pica')
        if name=='stereo' and args.smoke:sources.append(reuse_source)
        run([ROOT/'.toolchain/bin/picasso.exe','-o',binary,*sources])
        data=binary.read_bytes()
        with shader_c.open('a') as source:
            source.write(f'const unsigned char mp_{name}_shader[] __attribute__((aligned(4)))={{'+','.join(str(b) for b in data)+f'}};\nconst unsigned mp_{name}_shader_size={len(data)};\n')
    if args.smoke:
        reuse_binary=out/'stereo-reuse.shbin'
        run([ROOT/'.toolchain/bin/picasso.exe','-o',reuse_binary,shader_dir/'vertex.v.pica',reuse_source])
        data=reuse_binary.read_bytes()
        with shader_c.open('a') as source:
            source.write('const unsigned char mp_stereo_reuse_shader[] __attribute__((aligned(4)))={'+','.join(map(str,data))+'};\n'+f'const unsigned mp_stereo_reuse_shader_size={len(data)};\n')
        reference=out/'shader-reference'
        run([sys.executable,ROOT/'tools/generate_vertex_shader.py',*([] if args.identity_affine else ['--reference']),'--output',reference])
        for name,stem in [('base','vertex'),('dual','vertex-dual'),('stereo','vertex-stereo'),('dual_stereo','vertex-dual-stereo')]:
            binary=reference/f'{stem}.shbin'
            sources=[reference/f'{stem}.v.pica']
            if name in ('base','stereo'):sources.append(ROOT/'port/3ds/point.g.pica')
            run([ROOT/'.toolchain/bin/picasso.exe','-o',binary,*sources])
            data=binary.read_bytes()
            with shader_c.open('a') as source:
                source.write(f'const unsigned char mp_light_reference_{name}[] __attribute__((aligned(4)))={{'+','.join(str(b) for b in data)+f'}};\nconst unsigned mp_light_reference_{name}_size={len(data)};\n')
    extra=[]
    if args.feasibility_console:includes+=['-DMP_FEASIBILITY_AUTO']
    if args.render_worker:
        includes+=['-DMP_RENDER_WORKER']
        extra.append(ROOT/'port/3ds/render_worker.c')
    if args.clamped_shade:includes+=['-DMP_CLAMPED_SHADE_TEST']
    if args.render_rework:includes+=['-DMP_RENDER_REWORK_TEST']
    if args.async_presentation or args.release:
        from async_renderqueue import generate
        extra.append(generate(out))
        includes+=['-I'+str(ROOT/'port/3ds'),'-DMP_ASYNC_PRESENTATION']
    for src in (ROOT/'port/3ds/game.c',ROOT/'port/3ds/bottom.c',ROOT/'port/3ds/bottom_draw.c',ROOT/'port/3ds/renderer.c',ROOT/'port/3ds/early_queue.c',ROOT/'port/3ds/citro3d_fix.c',ROOT/'port/3ds/uniform_dispatch.c',ROOT/'port/3ds/services.c',ROOT/'port/3ds/cpu_speed.c',ROOT/'port/3ds/file_io.c',ROOT/'port/3ds/audio.c',ROOT/'port/3ds/command_cache.c',ROOT/'port/3ds/log_io.c',ROOT/'port/3ds/game_bridge.S',shader_c,*([ROOT/'tests/stereo_bounds_reference.c'] if args.smoke else []),*extra):
        obj=out/(src.stem+'.o')
        run([cc,*arch,*common_flags(),'-fshort-enums','-D__3DS__',
             *(['-DMP_SMOKE_TEST'] if args.smoke else []),*(['-DMP_AFFINE_IDENTITY_SHADER'] if args.identity_affine else []),*(['-DMP_UNLIT_AFFINE_SHADER'] if args.unlit_affine else []),*(['-DMP_BANNER_CAPTURE'] if args.banner_capture else []),*(['-DMP_AUDIO_HLE_TEST'] if args.audio_hle else []),*(['-DMP_BOOTMODE'] if args.boot else []),*includes,'-c',src,'-o',obj])
        objects.append(obj)
    libs=arm/'arm-none-eabi/lib/armv6k/fpu'
    gcc=sorted((arm/'lib/gcc/arm-none-eabi').iterdir())[-1]/'armv6k/fpu'
    script=out/'game.ld'
    from linker_layout import prepare_layout
    layout=prepare_layout(sdk)
    layout=layout.replace('.bss ALIGN(8)', '.bss ALIGN(32)')
    layout+='\nmp_image_text_start = ADDR(.text);\nmp_image_rodata_start = ADDR(.rodata);\nmp_image_data_start = ADDR(.data);\n'
    script.write_text(layout)
    linked=out/'melee-linked.elf';elf=out/'melee.elf'
    command=[bin/'ld.lld.exe','-T',script,'--gc-sections','--emit-relocs','--wrap=GX_ProcessCommandList',
             *('--wrap='+name for name in ('C3D_UpdateUniforms','C3Di_LoadShaderUniforms','C3Di_ClearShaderUniforms','C3Di_DirtyUniforms')),
             '--error-limit=0','-Map='+str(out/'melee.map'),
             libs/'3dsx_crt0.o',gcc/'crti.o',gcc/'crtbegin.o',*objects,
             '-L'+str(sdk/'libctru/lib'),'-L'+str(libs),'-L'+str(gcc),
             '--start-group',library,'-lcitro2d','-lcitro3d','-lctru','-lm','-lc','-lsysbase','-lgcc',
             '--end-group',gcc/'crtend.o',gcc/'crtn.o','-o',linked]
    run(command)
    from be8_image import prepare as prepare_image
    image_info=prepare_image(linked,elf)
    dest=args.output if args.output is not None else ROOT/('dist/native-alpha/3ds/melee/melee.3dsx' if args.release else 'dist/3ds/melee/melee-development.3dsx')
    if not dest.is_absolute():dest=ROOT/dest
    dest.parent.mkdir(parents=True,exist_ok=True)
    run([ROOT/'.toolchain/bin/3dsxtool.exe',elf,dest])
    print('Built native engine integration:',dest,'Build-time BE8 relocation words:',image_info['be8_words'])


if __name__=='__main__':main()
