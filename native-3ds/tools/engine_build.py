"""Compile the pinned Melee source into native ARM BE8 objects."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
from build import ROOT, UPSTREAM, prepare, common_flags, local_clang
from be8_object import convert
from engine_encoding import shift_jis_literals

LIBC = ('memcpy memset memmove memcmp memchr strlen strcmp strncmp strcpy strncpy '
        'strcat strchr strrchr strstr printf sprintf snprintf vsprintf vsnprintf '
        'puts putchar rand srand qsort sin cos tan asin acos atan atan2 sqrt pow '
        'floor ceil fabs fmod frexp ldexp modf exp log log10 sinf cosf tanf asinf '
        'acosf atanf atan2f sqrtf powf floorf ceilf fabsf fmodf expf logf log10f '
        'malloc free calloc realloc exit abort').split()


def compile_engine(jobs=6,sanitize=False):
    cc = local_clang()
    includes = prepare()
    out = ROOT / 'build/engine-be8'
    out.mkdir(parents=True,exist_ok=True)
    includes += ['-isystem',str(ROOT / '.toolchain/devkitpro/devkitARM/arm-none-eabi/include')]
    flags = [cc,'--no-default-config','--target=armeb-none-eabi','-mcpu=mpcore',
             '-mfpu=vfp','-mfloat-abi=hard','-mtp=soft',*common_flags(),
             '-fno-builtin','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
             '-DMP_GAME_ABI','-Dmain=mp_engine_main','-D__eabi=mp_engine_eabi',
             '-D__assert=mp_be_assert','-fno-short-enums',*('-D'+n+'=mp_be_'+n for n in LIBC),*includes]
    if sanitize:flags+=['-fsanitize=null']
    subprocess.run([*flags,'-fsyntax-only',str(ROOT/'tests/arm_abi.c')],check=True,capture_output=True)
    sources = sorted((UPSTREAM/'src/melee').rglob('*.c'))
    sources += [p for p in sorted((UPSTREAM/'src/sysdolphin').rglob('*.c')) if p.name != 'debug.c']
    sources.append(UPSTREAM/'extern/dolphin/src/dolphin/pad/Padclamp.c')
    sources += sorted((ROOT/'port/engine').glob('*.c'))
    from engine_math import generate
    sources.append(generate())
    pending=ROOT/'build/generated/gx_pending.c'
    if pending.exists(): sources.append(pending)
    sources = [p for p in sources if p.name != 'be_probe.c']
    headers = b''.join(p.read_bytes() for base in ('port/include','port/engine','build/compat')
                      for p in sorted((ROOT/base).rglob('*.h')))
    # The companion screen's wire schema is shared with the native SDK side.
    # Schema edits must invalidate BE8 objects as well as native UI objects.
    headers += (ROOT/'port/3ds/bottom_state.h').read_bytes()
    basehash = hashlib.sha256(json.dumps(flags).encode()+headers+(ROOT/'tools/engine_encoding.py').read_bytes()).digest()
    def compile_one(src):
        from engine_overlays import adapt
        selected=adapt(src)
        # Let ARM inline fixed-size game copies/comparisons. Any residual
        # memcpy/memset/memcmp call is retargeted to its BE8 implementation by
        # convert(). Keep the implementations themselves under their names.
        memory_flags=[]
        if src.is_relative_to(UPSTREAM):
            for name in ('memcpy','memset','memcmp'):
                memory_flags+=['-U'+name,'-D'+name+'=__builtin_'+name]
        rel = src.relative_to(ROOT).as_posix()
        stem = rel.replace('/','_').replace('.c','')
        raw = out/(stem+'.raw.o'); obj = out/(stem+'.o'); signature=out/(stem+'.sha256')
        digest = hashlib.sha256(basehash+json.dumps(memory_flags).encode()+selected.read_bytes()).hexdigest()
        if not raw.exists() or not signature.exists() or signature.read_text()!=digest:
            # Retail's sjiswrap gives the font engine two-byte glyphs. Clang
            # otherwise emits UTF-8 (including full-width English names).
            pp=out/(stem+'.i')
            p=subprocess.run([*flags,*memory_flags,'-g0','-I'+str(src.parent),'-Wno-everything','-E',str(selected),'-o',str(pp)],
                             capture_output=True,text=True,errors='replace')
            if p.returncode:return {'source':rel,'error':p.stderr}
            pp.write_text(shift_jis_literals(pp.read_text(encoding='utf-8')),encoding='utf-8')
            p = subprocess.run([*flags,*memory_flags,'-g0','-Wno-everything','-c',str(pp),'-o',str(raw)],
                               capture_output=True,text=True,errors='replace')
            if p.returncode: return {'source':rel,'error':p.stderr}
            signature.write_text(digest)
        n=convert(raw,obj)
        return {'source':rel,'object':str(obj),'fixups':n}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        results=list(pool.map(compile_one,sources))
    errors=[r for r in results if 'error' in r]
    (out/'report.json').write_text(json.dumps(results,indent=2))
    if errors:
        for e in errors: print(e['source'],e['error'][:3000])
        raise SystemExit(f'{len(errors)} engine files did not compile')
    objects=[r['object'] for r in results]
    archive=out/'libmelee-be8.a'
    if archive.exists(): archive.unlink()
    response=out/'objects.rsp'
    response.write_text('\n'.join('"'+p.replace('\\','/')+'"' for p in objects))
    ar=Path(cc).with_name('llvm-ar.exe')
    subprocess.run([str(ar),'rcs',str(archive),'@'+str(response)],check=True)
    print(f'Built {len(results)} ARM BE8 engine objects')
    return archive


if __name__=='__main__': compile_engine()
