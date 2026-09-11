"""Find undefined local reads in the adapted game without rebuilding its ELF."""
import concurrent.futures,json,subprocess
from build import ROOT,UPSTREAM,prepare,local_clang,common_flags
from engine_build import LIBC
from engine_overlays import adapt
flags=[local_clang(),'--no-default-config','--target=armeb-none-eabi','-mcpu=mpcore',
       '-mfpu=vfp','-mfloat-abi=hard','-mtp=soft',*common_flags(),'-fno-builtin',
       '-DMP_GAME_ABI','-fno-short-enums',*('-D'+n+'=mp_be_'+n for n in LIBC),*prepare(),
       '-isystem',str(ROOT/'.toolchain/devkitpro/devkitARM/arm-none-eabi/include'),
       '-Wno-everything','-Wuninitialized','-fsyntax-only']
sources=sorted((UPSTREAM/'src/melee').rglob('*.c'))+sorted((UPSTREAM/'src/sysdolphin').rglob('*.c'))
sources=[p for p in sources if p.name!='debug.c'] # Replaced by the native adapter.
def check(source):
    result=subprocess.run([*flags,'-I'+str(source.parent),str(adapt(source))],capture_output=True,text=True,errors='replace')
    return {'source':str(source.relative_to(ROOT)),'exit':result.returncode,'diagnostic':result.stderr}
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:results=list(pool.map(check,sources))
warnings=[r for r in results if r['diagnostic']]
(ROOT/'build/uninitialized-audit.json').write_text(json.dumps(warnings,indent=2))
print(json.dumps({'checked':len(results),'files_with_diagnostics':len(warnings),'files':[r['source'] for r in warnings]}))
