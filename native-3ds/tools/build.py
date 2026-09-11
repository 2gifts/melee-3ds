"""Build/test port components. No original game data is required for diagnostics."""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream/melee"
CORE = [ROOT / f"port/core/{name}.c" for name in ("archive", "input", "engine")]
ENGINE = [UPSTREAM / f"src/sysdolphin/baselib/{name}.c"
          for name in ("random", "spline")]


def run(args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def prepare():
    lock = json.loads((ROOT / "upstream.lock.json").read_text())
    if not (UPSTREAM / "src/Runtime/platform.h").exists():
        raise SystemExit("Missing upstream source; run python tools/bootstrap.py")
    rev = subprocess.check_output(["git", "-C", str(UPSTREAM), "rev-parse", "HEAD"],
                                  text=True).strip()
    if rev != lock["commit"]:
        raise SystemExit(f"Upstream revision mismatch: {rev}")
    # A generated header overlay keeps the decomp checkout untouched. The
    # standard library owns ssize_t on both newlib and the desktop test host.
    src = (UPSTREAM / "src/Runtime/platform.h").read_text()
    old = "typedef signed int ssize_t;"
    if src.count(old) != 1:
        raise SystemExit("Upstream platform.h changed; review compatibility patch")
    src = src.replace(old, "#include <sys/types.h>")
    src = src.replace("#include <stdbool.h>",
                      "#ifdef MP_GAME_ABI\n#include <MSL/stdbool.h>\n"
                      "_Static_assert(sizeof(bool) == 4, \"GameCube boolean ABI\");\n"
                      "#else\n#include <stdbool.h>\n#endif")
    for macro in ("U64_MAX", "M_TAU"):
        import re
        src = re.sub(r"(^#define " + macro + r" [^\n]+)$",
                     r"#ifndef " + macro + r"\n\1\n#endif", src, flags=re.M)
    target = ROOT / "build/compat/Runtime/platform.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(src)
    placeholder=(UPSTREAM/'src/placeholder.h').read_text()
    old='#define __frsqrte(x) sqrt(x)'
    assert placeholder.count(old)==1, 'Reciprocal-square-root placeholder changed'
    placeholder=placeholder.replace(old,'double mp_frsqrte(double);\n#define __frsqrte(x) mp_frsqrte(x)')
    (ROOT/'build/compat/placeholder.h').write_text(placeholder)
    vectors=json.loads((ROOT/'tests/fixtures/frsqrte.json').read_text())['vectors']
    (ROOT/'build/compat/ppc_math_vectors.h').write_text(
        'static const uint64_t mp_ppc_vectors[][2]={\n'+
        ',\n'.join('{0x'+a+'ULL,0x'+b+'ULL}' for a,b in vectors)+'\n};\n')
    rtc = UPSTREAM / "extern/dolphin/include/dolphin/os/OSRtc.h"
    target = ROOT / "build/compat/dolphin/os/OSRtc.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(rtc.read_text().replace("unsigned long OSGetSoundMode()",
                                            "u32 OSGetSoundMode(void)")
                      .replace("OSSetSoundMode(unsigned long mode)", "OSSetSoundMode(u32 mode)"))
    mtx = UPSTREAM / "extern/dolphin/include/dolphin/mtx.h"
    target = ROOT / "build/compat/dolphin/mtx.h"
    target.write_text(mtx.read_text().replace(
        "void MTXPerspective(Mtx m, f32 fovY, f32 aspect, f32 n, f32 f);\n", ""))
    vert=UPSTREAM/'extern/dolphin/include/dolphin/gx/GXVert.h'
    target=ROOT/'build/compat/dolphin/gx/GXVert.h'
    target.parent.mkdir(parents=True,exist_ok=True)
    contents=vert.read_text()
    declarations='\n'.join('void mp_gx_write_'+t+'('+t+' value);' for t in
                           ('u8','s8','u16','s16','u32','s32','u64','s64','f32','f64'))
    contents=contents.replace('// inline functions','// Native FIFO writer hooks\n'+declarations+'\n// inline functions')
    import re
    contents=re.sub(r'GXWGFifo\.T = ([xyzw]);',r'mp_gx_write_##T(\1);',contents)
    contents=contents.replace('GXWGFifo.u8 = x;','mp_gx_write_u8(x);').replace('GXWGFifo.u16 = x;','mp_gx_write_u16(x);')
    target.write_text(contents)
    os_source=UPSTREAM/'extern/dolphin/include/dolphin/os.h'
    os_header=os_source.read_text().replace('#define OS_CACHED_REGION_PREFIX 0x8000','#define OS_CACHED_REGION_PREFIX 0')
    os_header=os_header.replace('#define OS_UNCACHED_REGION_PREFIX 0xC000','#define OS_UNCACHED_REGION_PREFIX 0')
    os_header=os_header.replace('#define __OSBusClock (*(u32*) (OS_BASE_CACHED | 0x00F8))','#define __OSBusClock 162000000u')
    os_header=os_header.replace('#define __OSCoreClock (*(u32*) (OS_BASE_CACHED | 0x00FC))','#define __OSCoreClock 486000000u')
    (ROOT/'build/compat/dolphin/os.h').write_text(os_header)
    return ["-I" + str(p) for p in (
        ROOT / "build/compat", ROOT / "port/include", UPSTREAM / "src",
        ROOT / "build/generated", UPSTREAM / "extern/dolphin/include")]


def local_clang():
    found = sorted((ROOT / ".toolchain").glob("llvm-mingw-*/bin/clang.exe"))
    return str(found[-1]) if found else shutil.which("clang") or shutil.which("cc")


def common_flags():
    return ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra",
            "-Wno-unused-variable", "-Wno-unknown-pragmas", "-fno-strict-aliasing",
            "-fwrapv", "-ffp-contract=off", "-ffunction-sections", "-fdata-sections"]


def host(cc, includes):
    out = ROOT / "build/host"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ("core_tests.exe" if os.name == "nt" else "core_tests")
    run([cc, *common_flags(), "-DMP_GAME_ABI", "-fno-short-enums", *includes, *CORE, *ENGINE,
         ROOT / "tests/core_tests.c", "-lm", "-o", exe])
    run([exe])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("target", choices=("host", "3ds", "3ds-clang"))
    ap.add_argument("--cc")
    ap.add_argument("--smoke", action="store_true", help="3DS test build: capture framebuffers and exit at frame 180")
    ap.add_argument("--be8", action="store_true", help="Exercise original archive loader in ARM BE8 mode")
    args = ap.parse_args()
    includes = prepare()
    if args.target == "host":
        cc = args.cc or local_clang()
        if not cc:
            ap.error("Install a C compiler or pass --cc")
        host(cc, includes)
    else:
        build_3ds(args.target, args.cc, includes, args.smoke, args.be8)


def build_3ds(target, cc, includes, smoke=False, be8=False):
    clang = target == "3ds-clang"
    sdk = Path(os.environ.get("DEVKITPRO", ROOT / ".toolchain/devkitpro"))
    arm = Path(os.environ.get("DEVKITARM", sdk / "devkitARM"))
    suffix = ".exe" if os.name == "nt" else ""
    cc = cc or (local_clang() if clang else arm / "bin" / ("arm-none-eabi-gcc" + suffix))
    if not cc or not Path(cc).exists():
        raise SystemExit("Install devkitPro 3ds-dev and set DEVKITPRO/DEVKITARM")
    out = ROOT / ("build/3ds-smoke" if smoke else "build/3ds")
    out.mkdir(parents=True, exist_ok=True)
    includes = [*includes, "-I" + str(sdk / "libctru/include")]
    arch = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
    if clang:
        arch = ["--no-default-config", "--target=arm-none-eabi", "-mcpu=mpcore",
                "-mfpu=vfp", "-mfloat-abi=hard", "-mtp=soft"]
        includes += ["-isystem", str(arm / "arm-none-eabi/include")]
    objects = []
    commands = []
    for src in [*CORE, *ENGINE, ROOT / "port/3ds/main.c"]:
        obj = out / (src.stem + ".o")
        abi = ["-fshort-enums"] if src == ROOT / "port/3ds/main.c" else ["-DMP_GAME_ABI", "-fno-short-enums"]
        if smoke: abi += ["-DMP_SMOKE_TEST"]
        if be8: abi += ["-DMP_BE8_TEST"]
        cmd = [cc, *arch, *common_flags(), *abi, "-D__3DS__", *includes, "-c", src, "-o", obj]
        commands.append({"directory": str(ROOT), "file": str(src),
                         "arguments": list(map(str, cmd))})
        run(cmd)
        objects.append(obj)
    if be8:
        if not clang: raise SystemExit("BE8 path currently requires the portable Clang route")
        from be8_object import convert
        be_arch = [arg.replace('arm-none-eabi','armeb-none-eabi') for arg in arch]
        for src in [UPSTREAM / 'src/sysdolphin/baselib/archive.c',
                    UPSTREAM / 'src/sysdolphin/baselib/random.c',ROOT / 'port/engine/be_probe.c']:
            obj = out / ('be_' + src.stem + '.o')
            raw = out / ('be_' + src.stem + '.raw.o')
            cmd = [cc,*be_arch,*common_flags(),'-DMP_GAME_ABI','-fno-short-enums',
                   '-fno-builtin','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
                   '-Dmemcpy=mp_be_memcpy','-Dmemset=mp_be_memset','-Dstrcmp=mp_be_strcmp',
                   '-DOSReport=mp_be_report','-DHSD_Rand=mp_be_HSD_Rand',
                   '-Dseed=mp_be_seed','-Dseed_ptr=mp_be_seed_ptr',
                   '-DHSD_Randf=mp_be_HSD_Randf','-DHSD_Randi=mp_be_HSD_Randi',
                   '-D_HSD_RandForgetMemory=mp_be_RandForgetMemory',*includes,'-c',src,'-o',raw]
            run(cmd)
            print('BE8 fixups:',convert(raw,obj),src.name)
            objects.append(obj)
        obj = out / 'be_entry.o'
        run([cc,*arch,'-c',ROOT / 'port/3ds/be_entry.S','-o',obj])
        objects.append(obj)
    run([cc, *arch, "-std=gnu11", "-fshort-enums", "-D__3DS__", *includes,
         "-fsyntax-only", ROOT / "tests/3ds_abi.c"])
    run([cc, *arch, "-std=gnu11", "-fno-short-enums", "-DMP_GAME_ABI", *includes,
         "-fsyntax-only", ROOT / "tests/arm_abi.c"])
    (out / "compile_commands.json").write_text(json.dumps(commands, indent=2))
    name = "melee-smoke" if smoke else "melee-diagnostics"
    elf = out / (name + ".elf")
    if clang:
        # Experimental Clang build using the official SDK's CRT, newlib and
        # libgcc. Normal supported builds use the devkitARM specs below.
        libs = arm / "arm-none-eabi/lib/armv6k/fpu"
        gcc_versions = sorted((arm / "lib/gcc/arm-none-eabi").iterdir())
        gcc = gcc_versions[-1] / "armv6k/fpu"
        linker = Path(cc).with_name("ld.lld" + suffix)
        script = arm / "arm-none-eabi/lib/3dsx.ld"
        # GNU ld accepts implicit TLS layout here; lld requires an explicit
        # PT_TLS segment. 3dsxtool consumes only the three PT_LOAD segments.
        layout = script.read_text()
        layout = layout.replace("data   PT_LOAD FLAGS(6)",
                                "tls    PT_TLS FLAGS(4);\n\tdata   PT_LOAD FLAGS(6)")
        for start, end in (("\t.tdata :", "\t.tbss :"),
                           ("\t.tbss :", "\t/*")):
            a = layout.index(start)
            b = layout.index(end, a + len(start))
            layout = layout[:a] + layout[a:b].replace(": data", ": data : tls") + layout[b:]
        layout = layout.replace(".bss ALIGN(4)", ".bss ALIGN(8)")
        # lld otherwise orphans .got after __bss_start__, where libctru's CRT
        # clears it. Keep initialized GOT entries in the data segment.
        layout = layout.replace("*(.data.*)", "*(.data.*)\n\t\t*(.got .got.*)")
        if be8:
            layout = layout.replace('*(.rodata.*)',
                '*(.rodata.*)\n . = ALIGN(4); mp_be_fixups_start = .; KEEP(*(.mp_be_fixups)) mp_be_fixups_end = .;')
        script = out / "3dsx-lld.ld"
        script.write_text(layout)
        link_command = [linker, "-T", script, "--gc-sections", "--emit-relocs",
             "-Map=" + str(out / "melee-diagnostics.map"),
             libs / "3dsx_crt0.o", gcc / "crti.o", gcc / "crtbegin.o", *objects,
             "-L" + str(sdk / "libctru/lib"), "-L" + str(libs), "-L" + str(gcc),
             "--start-group", "-lcitro2d", "-lcitro3d", "-lctru", "-lm", "-lc",
             "-lsysbase", "-lgcc", "--end-group", gcc / "crtend.o", gcc / "crtn.o",
             "-o", elf]
        run(link_command)
        if be8:
            nm = Path(cc).with_name('llvm-nm' + suffix)
            symbols = subprocess.check_output([str(nm),'--defined-only',str(elf)],text=True)
            fixes = [line.split()[-1] for line in symbols.splitlines()
                     if line.split() and line.split()[-1].startswith('mp_be_fix_')]
            fix_source = out / 'be_fixups.S'
            fix_source.write_text('.section .mp_be_fixups,"a",%progbits\n.balign 4\n' +
                                  ''.join('.word '+label+'\n' for label in fixes))
            fix_object = out / 'be_fixups.o'
            run([cc,*arch,'-c',fix_source,'-o',fix_object])
            run([*link_command,fix_object])
    else:
        run([cc, *arch, "-specs=3dsx.specs", *objects,
             "-L" + str(sdk / "libctru/lib"),
             "-Wl,-Map," + str(out / "melee-diagnostics.map"),
             "-lcitro2d", "-lcitro3d", "-lctru", "-lm", "-o", elf])
    packer = sdk / "tools/bin" / ("3dsxtool" + suffix)
    if not packer.exists():
        packer = ROOT / ".toolchain/bin" / ("3dsxtool" + suffix)
    if not packer.exists():
        raise SystemExit("ARM ELF built; 3dsxtool required to package Homebrew Launcher output")
    dist = ROOT / "dist/3ds/melee"
    dist.mkdir(parents=True, exist_ok=True)
    run([packer, elf, dist / (name + ".3dsx")])
    print("Built diagnostics only (not playable Melee):", dist / (name + ".3dsx"))


if __name__ == "__main__":
    main()
