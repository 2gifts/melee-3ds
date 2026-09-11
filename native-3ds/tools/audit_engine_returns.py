"""Find original-engine functions whose implicit PPC result is lost on ARM."""
import concurrent.futures, json, subprocess
from build import ROOT, local_clang

def check(path):
    result=subprocess.run([local_clang(),'--no-default-config','--target=armeb-none-eabi',
        '-mcpu=mpcore','-mfpu=vfp','-mfloat-abi=hard','-std=gnu11',
        '-fno-short-enums','-fsyntax-only','-Wno-everything','-Wreturn-type',str(path)],
        capture_output=True,text=True,errors='replace')
    return {'source':path.name,'status':result.returncode,'diagnostics':result.stderr} if result.stderr or result.returncode else None

if __name__=='__main__':
    files=list((ROOT/'build/engine-be8').glob('upstream_*.i'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        findings=[r for r in pool.map(check,files) if r]
    report={'files_checked':len(files),'findings':findings}
    (ROOT/'build/engine-return-audit.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
