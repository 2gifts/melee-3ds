"""Build an isolated stereo-reuse experiment; never packages the game or CIA.

The default prototype demonstrates the missing inner-eye clipping problem.
--clip tests a polygon clipper before deciding whether to integrate it. The
game's production stereo renderer is untouched.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess
from build import ROOT, local_clang
from linker_layout import prepare_layout

OUT = ROOT/'build/stereo-geometry-probe'


def clipped_shader():
    """Clip each source edge against -W <= Y <= W, then emit the polygon fan.

    Each emitted point is on the original triangle's boundary. The GPU output
    buffer keeps the fan anchor while slots 1/2 alternate, avoiding a second
    in-register polygon buffer. Position, color and UV use the same edge t.
    """
    s=['.gsh point c0','.fvec stereo_params','.constf constants(0.5,0.0,1.0,-1.0)',
       '.out position position','.out color color','.out uv texcoord0',
       '.entry stereo_main','.proc stereo_main','    mov r14, constants',
       '    mov r13, stereo_params']
    for eye in range(2):
        s+=['    mov r15, r14.yyyy']
        if eye:s+=['    mov r13.xy, -r13','    mov r13.w, -r13.w']
        # Most fighter triangles fit inside both eye edges. Preserve their
        # original vertex order and bypass all polygon interpolation/calls.
        for n in (0,3,6):
            s += [f'    mov r{n}, v{n}',
                  f'    mad r{n}.y, r13.x, r{n}.w, r{n}.y',f'    add r{n}.y, r{n}.y, r13.y',
                  f'    add r9.x, r{n}.w, -r{n}.y',f'    add r9.y, r{n}.w, r{n}.y',
                  '    cmp r9, ge, ge, r14.yyyy',f'    jmpc !cmp.x, eye{eye}_clip',f'    jmpc !cmp.y, eye{eye}_clip']
        for vertex,n in enumerate((0,3,6)):
            s += [f'    mul r11.y, r13.w, r{n}.w',f'    mad r{n}.y, r13.z, r{n}.y, r11.y',
                  f'    setemit {vertex}'+(', prim' if vertex==2 else ''),
                  f'    mov position, r{n}',f'    mov color, v{n+1}',f'    mov uv, v{n+2}','    emit']
        s += ['    cmp r14.z, eq, eq, r14.z',f'    jmpc cmp.x, eye{eye}_next',f'eye{eye}_clip:']
        for a,b in ((0,1),(1,2),(2,0)):
            for dest,source in ((0,a*3),(1,a*3+1),(2,a*3+2),(3,b*3),(4,b*3+1),(5,b*3+2)):
                s += [f'    mov r{dest}, v{source}']
            for n in (0,3):
                s += [f'    mad r{n}.y, r13.x, r{n}.w, r{n}.y',f'    add r{n}.y, r{n}.y, r13.y']
            s+=['    call clip_edge']
        s += [f'eye{eye}_next:']
    s+=['    end','.end','.proc clip_edge','    mov r12.x, r14.y','    mov r12.y, r14.z']
    for plane in range(2):
        if not plane:s+=['    add r9.x, r0.y, r0.w','    add r9.y, r3.y, r3.w']
        else:s+=['    add r9.x, r0.w, -r0.y','    add r9.y, r3.w, -r3.y']
        s += ['    cmp r9, ge, ge, r14.yyyy',f'    jmpc !cmp.x, plane{plane}_a_out',
              f'    jmpc !cmp.y, plane{plane}_b_out',f'    jmpc cmp.x, plane{plane}_next',
              f'plane{plane}_a_out:', '    jmpc !cmp.y, edge_done',
              '    add r10.x, r9.x, -r9.y','    rcp r10.x, r10.x','    mul r10.x, r10.x, r9.x',
              '    max r12.x, r12.x, r10.x',f'    jmpc !cmp.x, plane{plane}_next',
              f'plane{plane}_b_out:',
              '    add r10.x, r9.x, -r9.y','    rcp r10.x, r10.x','    mul r10.x, r10.x, r9.x',
              '    min r12.y, r12.y, r10.x',f'plane{plane}_next:']
    s += ['    cmp r12.x, gt, gt, r12.y','    jmpc cmp.x, edge_done',
          '    cmp r12.x, gt, gt, r14.y','    ifc cmp.x',
          '        mov r10.x, r12.x','        call interpolate','        call emit_point','    .end',
          '    cmp r12.y, lt, lt, r14.z','    ifc cmp.x','        mov r10.x, r12.y',
          '        call interpolate','    .else','        mov r6, r3','        mov r7, r4','        mov r8, r5','    .end',
          '    call emit_point','edge_done:','.end','.proc interpolate',
          '    add r6, r3, -r0','    mad r6, r6, r10.x, r0',
          '    add r7, r4, -r1','    mad r7, r7, r10.x, r1',
          '    add r8, r5, -r2','    mad r8, r8, r10.x, r2','.end',
          '.proc emit_point','    cmp r15.x, eq, eq, r14.y','    jmpc cmp.x, emit_first',
          '    cmp r15.x, eq, eq, r14.z','    jmpc cmp.x, emit_second',
          '    cmp r15.y, eq, eq, r14.y','    ifc cmp.x','        setemit 2, prim','    .else',
          '        setemit 1, inv prim','    .end','    add r15.y, r14.z, -r15.y',
          '    cmp r14.z, eq, eq, r14.z','    jmpc cmp.x, emit_output',
          'emit_first:','    setemit 0','    jmpc cmp.x, emit_output',
          'emit_second:','    setemit 1','emit_output:',
          '    mul r11.y, r13.w, r6.w','    mad r6.y, r13.z, r6.y, r11.y',
          '    mov position, r6','    mov color, r7','    mov uv, r8','    emit',
          '    add r15.x, r15.x, r14.z','.end','']
    return '\n'.join(s)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--clip',action='store_true');ap.add_argument('--bounded',action='store_true')
    ap.add_argument('--shared-vsh',action='store_true')
    ap.add_argument('--viewport-control',action='store_true',help='Add a third mode: same combined viewport, ordinary per-eye vertex shading')
    ap.add_argument('--stress',action='store_true',help='128 deterministic perspective/color/texture pages; skip the separate CPU microbenchmark')
    ap.add_argument('--fixed-parameters',action='store_true',help='Match geometry uniform eye offsets to the SDK packed fixed attributes')
    ap.add_argument('--coherent-depth',action='store_true',help='Keep each stress quad within a narrow depth interval to exercise more reused batches')
    ap.add_argument('--early-queue',action='store_true',help='Compare deferred versus early submission using the same ordinary stereo shader')
    args=ap.parse_args()
    if args.clip and args.bounded:ap.error('--bounded uses the short shader, not the experimental clipper')
    if args.shared_vsh and not args.bounded:ap.error('--shared-vsh requires --bounded')
    if args.early_queue and not args.bounded:ap.error('--early-queue requires --bounded for all 24 fixtures')
    if (args.viewport_control or args.stress) and not (args.bounded and args.shared_vsh and not args.early_queue):ap.error('Precision controls require --bounded --shared-vsh and exclude --early-queue')
    if args.coherent_depth and not args.stress:ap.error('--coherent-depth requires --stress')
    OUT.mkdir(parents=True, exist_ok=True)
    shader = ['; Experimental two-eye emission after one Melee vertex shader.',
              '.gsh point c0', '.fvec stereo_params',
              '.constf constants(0.5,0.0,1.0,-1.0)',
              '.out position position', '.out color color', '.out uv texcoord0',
              '.entry stereo_main', '.proc stereo_main',
              '    mov r2, stereo_params', '    mov r3, constants']
    for eye in range(2):
        for vertex in range(3):
            n = vertex*3
            shader += [f'    mov r0, v{n}',
                       f'    mad r0.y, {"-" if eye else ""}r2.x, r0.w, r0.y',
                       f'    add r0.y, r0.y, {"-" if eye else ""}r2.y',
                       '    mul r1.y, r2.w, r0.w',
                       f'    mad r0.y, r3.x, r0.y, {"-" if eye else ""}r1.y',
                       f'    setemit {vertex}'+(', prim' if vertex==2 else ''),
                       '    mov position, r0', f'    mov color, v{n+1}',
                       f'    mov uv, v{n+2}', '    emit']
    shader += ['    end', '.end', '']
    (OUT/'stereo.g.pica').write_text(clipped_shader() if args.clip else '\n'.join(shader))
    binaries = []
    programs=[('reference', [ROOT/'port/3ds/vertex-stereo.v.pica']),
                          ('candidate', [ROOT/('port/3ds/vertex-stereo.v.pica' if args.shared_vsh else 'port/3ds/vertex.v.pica'),
                                         *([ROOT/'port/3ds/point.g.pica'] if args.shared_vsh else []),OUT/'stereo.g.pica'])]
    if args.viewport_control:
        original=(ROOT/'port/3ds/vertex-stereo.v.pica').read_text()
        assert original.count('    mov outpos, r11\n')==3
        control=original.replace('    mov outpos, r11\n','    mul r12.y, v6.z, r11.w\n    mad r11.y, v6.w, r11.y, r12.y\n    mov outpos, r11\n')
        (OUT/'viewport-control.v.pica').write_text(control)
        programs.append(('viewport_control',[OUT/'viewport-control.v.pica']))
    for name,sources in programs:
        binary = OUT/(name+'.shbin')
        subprocess.run([str(ROOT/'.toolchain/bin/picasso.exe'), '-o', str(binary), *map(str,sources)], check=True)
        data = binary.read_bytes()
        binaries.append(f'const unsigned char {name}[] __attribute__((aligned(4)))={{'+','.join(map(str,data))+'};\n'+f'const unsigned {name}_size={len(data)};')
    (OUT/'shaders.c').write_text('\n'.join(binaries))
    cc = Path(local_clang()); sdk = ROOT/'.toolchain/devkitpro'; arm = sdk/'devkitARM'
    arch = ['--no-default-config','--target=arm-none-eabi','-mcpu=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft']
    objects = []
    for source in [ROOT/'tests/stereo_geometry_probe.c', OUT/'shaders.c',
                   *([ROOT/'port/3ds/early_queue.c'] if args.early_queue else []),
                   *([ROOT/'tests/stereo_bounds_reference.c'] if args.bounded else [])]:
        obj = OUT/(source.stem+'.o')
        subprocess.run([str(cc),*arch,'-O2','-g','-std=gnu11','-fshort-enums','-D__3DS__',
                        *(['-DMP_STEREO_BOUNDED'] if args.bounded else []),
                        *(['-DMP_STEREO_SHARED_VSH'] if args.shared_vsh else []),
                        *(['-DMP_VIEWPORT_CONTROL'] if args.viewport_control else []),
                        *(['-DMP_STEREO_STRESS'] if args.stress else []),
                        *(['-DMP_STEREO_FIXED_PARAMETERS'] if args.fixed_parameters else []),
                        *(['-DMP_STEREO_COHERENT_DEPTH'] if args.coherent_depth else []),
                        *(['-DMP_EARLY_QUEUE_PROBE','-DMP_SMOKE_TEST'] if args.early_queue else []),
                        '-I'+str(ROOT/'port/3ds'),
                        '-Wall','-Wextra','-Werror','-Wno-ignored-attributes','-ffp-contract=off','-ffunction-sections','-fdata-sections',
                        '-I'+str(sdk/'libctru/include'),'-isystem',str(arm/'arm-none-eabi/include'),
                        '-c',str(source),'-o',str(obj)],check=True)
        objects.append(obj)
    script = OUT/'probe.ld'; script.write_text(prepare_layout(sdk))
    libs = arm/'arm-none-eabi/lib/armv6k/fpu'
    gcc = sorted((arm/'lib/gcc/arm-none-eabi').iterdir())[-1]/'armv6k/fpu'
    elf = OUT/'probe.elf'
    command = [cc.with_name('ld.lld.exe'),'-T',script,'--gc-sections','--emit-relocs',
               libs/'3dsx_crt0.o',gcc/'crti.o',gcc/'crtbegin.o',*objects,
               '-L'+str(sdk/'libctru/lib'),'-L'+str(libs),'-L'+str(gcc),
               '--start-group','-lcitro3d','-lctru','-lm','-lc','-lsysbase','-lgcc','--end-group',
               gcc/'crtend.o',gcc/'crtn.o','-o',elf]
    subprocess.run(list(map(str,command)),check=True)
    subprocess.run([str(ROOT/'.toolchain/bin/3dsxtool.exe'),str(elf),str(OUT/'probe.3dsx')],check=True)
    (OUT/'build.json').write_text(json.dumps(dict(experimental=True,integrated_into_game=False,
        clipping_implemented=args.clip,
        bounded=args.bounded,shared_vsh=args.shared_vsh,early_queue=args.early_queue,fixtures=128 if args.stress else 24 if args.bounded else 10,
        modes=3 if args.viewport_control else 2,viewport_control=args.viewport_control,stress=args.stress,
        fixed_parameters=args.fixed_parameters,coherent_depth=args.coherent_depth,
        source_shader='Current full Melee shader; same matrices, colors and lighting',
        sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                 (Path(__file__),ROOT/'tests/stereo_geometry_probe.c',ROOT/'tests/stereo_bounds_reference.c',ROOT/'port/3ds/stereo_bounds.h',ROOT/'port/3ds/stereo_fixed.h',ROOT/'port/3ds/vertex.v.pica',ROOT/'port/3ds/vertex-stereo.v.pica',ROOT/'port/3ds/point.g.pica',OUT/'stereo.g.pica',*([OUT/'viewport-control.v.pica'] if args.viewport_control else []),*([ROOT/'port/3ds/early_queue.c',ROOT/'port/3ds/early_queue.h'] if args.early_queue else []))},
        binary_sha256=hashlib.sha256((OUT/'probe.3dsx').read_bytes()).hexdigest()),indent=2)+'\n')
    print('Built isolated stereo geometry probe:',OUT/'probe.3dsx')


if __name__=='__main__': main()
