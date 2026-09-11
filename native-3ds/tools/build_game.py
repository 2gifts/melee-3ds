"""Build the in-progress native Melee engine integration for 3DS."""
import argparse
import subprocess
from pathlib import Path
from build import ROOT, prepare, local_clang, common_flags, run
from engine_build import compile_engine


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--smoke',action='store_true')
    ap.add_argument('--audio-hle',action='store_true',help='Exercise NDSP using Azahar HLE without DSP firmware (smoke builds only)')
    ap.add_argument('--release',action='store_true',help='Build a separate homebrew package with physical controls')
    ap.add_argument('--skip-engine',action='store_true')
    ap.add_argument('--sanitize',action='store_true',help='Stop with a source location on original-engine null accesses')
    ap.add_argument('--boot',action='store_true',help='Enter Melee original main and scene loop')
    ap.add_argument('--output',type=Path,help='Alternate 3DSX destination, preserving an existing hardware test package')
    args=ap.parse_args()
    if args.audio_hle and not args.smoke:ap.error('--audio-hle requires --smoke; it cannot run on hardware')
    if args.release:
        if args.smoke:ap.error('--release and --smoke are mutually exclusive')
        args.boot=True
    library=ROOT/'build/engine-be8/libmelee-be8.a'
    if not args.skip_engine: library=compile_engine(sanitize=args.sanitize)
    out=ROOT/('build/game-release' if args.release else 'build/game');out.mkdir(parents=True,exist_ok=True)
    cc=local_clang();bin=Path(cc).parent
    sdk=ROOT/'.toolchain/devkitpro';arm=sdk/'devkitARM'
    arch=['--no-default-config','--target=arm-none-eabi','-mcpu=mpcore',
          '-mfpu=vfp','-mfloat-abi=hard','-mtp=soft']
    includes=[*prepare(),'-I'+str(sdk/'libctru/include'),'-isystem',str(arm/'arm-none-eabi/include')]
    shader=out/'vertex.shbin'
    run([ROOT/'.toolchain/bin/picasso.exe','-o',shader,ROOT/'port/3ds/vertex.v.pica',ROOT/'port/3ds/point.g.pica'])
    shader_c=out/'shader.c'
    blob=shader.read_bytes()
    shader_c.write_text('const unsigned char mp_shader[] __attribute__((aligned(4)))={'+
                        ','.join(str(b) for b in blob)+'};\nconst unsigned mp_shader_size='+str(len(blob))+';\n')
    dual_shader=out/'vertex-dual.shbin'
    run([ROOT/'.toolchain/bin/picasso.exe','-o',dual_shader,ROOT/'port/3ds/vertex-dual.v.pica'])
    dual_blob=dual_shader.read_bytes()
    with shader_c.open('a') as source:
        source.write('const unsigned char mp_dual_shader[] __attribute__((aligned(4)))={'+
                     ','.join(str(b) for b in dual_blob)+'};\nconst unsigned mp_dual_shader_size='+str(len(dual_blob))+';\n')
    objects=[]
    for name in ('stereo','dual_stereo'):
        binary=out/f'vertex-{name}.shbin'
        sources=[ROOT/f'port/3ds/vertex-{name.replace("_","-")}.v.pica']
        if name=='stereo':sources.append(ROOT/'port/3ds/point.g.pica')
        run([ROOT/'.toolchain/bin/picasso.exe','-o',binary,*sources])
        data=binary.read_bytes()
        with shader_c.open('a') as source:
            source.write(f'const unsigned char mp_{name}_shader[] __attribute__((aligned(4)))={{'+','.join(str(b) for b in data)+f'}};\nconst unsigned mp_{name}_shader_size={len(data)};\n')
    for src in (ROOT/'port/3ds/game.c',ROOT/'port/3ds/renderer.c',ROOT/'port/3ds/citro3d_fix.c',ROOT/'port/3ds/services.c',ROOT/'port/3ds/cpu_speed.c',ROOT/'port/3ds/file_io.c',ROOT/'port/3ds/audio.c',ROOT/'port/3ds/command_cache.c',ROOT/'port/3ds/log_io.c',ROOT/'port/3ds/game_bridge.S',shader_c):
        obj=out/(src.stem+'.o')
        run([cc,*arch,*common_flags(),'-fshort-enums','-D__3DS__',
             *(['-DMP_SMOKE_TEST'] if args.smoke else []),*(['-DMP_AUDIO_HLE_TEST'] if args.audio_hle else []),*(['-DMP_BOOTMODE'] if args.boot else []),*includes,'-c',src,'-o',obj])
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
