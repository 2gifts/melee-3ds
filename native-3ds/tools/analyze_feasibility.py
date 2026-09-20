"""Decode sparse console captures using the exact ELF recorded in a manifest.

Exclusive times partition the instrumented scopes, not all CPU work. Worker/GPU
times overlap. Sampled frame costs are estimates, never retail FPS predictions.
"""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
from build import ROOT, local_clang


def fields(line):
    return dict(re.findall(r'(\w+)=([0-9a-f]+)', line))


def parse(text, symbols):
    windows=[]; pending={}; rejected=[]
    for line in text.splitlines():
        if line.startswith('FeasibilityResult '):
            p={k:int(v) for k,v in fields(line).items()};p['samples']=[]
            p['ticks']=(p['ticks_hi']<<32)|p['ticks_lo']
            pending[p['id']]=p
        elif line.startswith('FeasibilityRow '):
            p=fields(line);ident=int(p['id'])
            if ident not in pending:continue
            row={k:int(v,16 if k=='fn' else 10) for k,v in p.items() if k!='id'}
            for name in ('inclusive','self'):row[name]=(row[name+'_hi']<<32)|row[name+'_lo']
            row['function']=symbols.get(row['fn'],f"unknown_{row['fn']:08x}")
            pending[ident]['samples'].append(row)
        elif line.startswith('FeasibilityEnd '):
            ident=int(fields(line)['id']);p=pending.pop(ident,None)
            if p is None:continue
            reason=[]
            if p['errors'] or p['overflow']:reason.append('Profiler accounting error/overflow')
            if len(p['samples'])!=p['rows']:reason.append('Missing log rows')
            if not 0<p['sampled']<=p['frames'] or not p['ticks']:reason.append('Invalid frame count/timer')
            keys=[(r['domain'],r['fn'],r['context']) for r in p['samples']]
            if len(set(keys))!=len(keys):reason.append('Duplicate row')
            if any(r['self']>r['inclusive'] for r in p['samples']):reason.append('Invalid exclusive time')
            if reason:rejected.append(dict(id=ident,reasons=reason));continue
            p['mean_instrumented_frame_ms']=p['ticks']/40500/p['frames']
            for r in p['samples']:
                r['inclusive_ms_per_sampled_render']=r['inclusive']/40500/p['sampled']
                r['own_ms_per_sampled_render']=r['self']/40500/p['sampled']
                r['mean_ms_per_call']=r['inclusive']/40500/r['calls'] if r['calls'] else None
            p['samples'].sort(key=lambda r:r['self'],reverse=True)
            windows.append(p)
    rejected += [dict(id=k,reasons=['Incomplete capture; no end marker']) for k in pending]
    return dict(windows=windows,rejected=rejected)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log',type=Path);ap.add_argument('--manifest',type=Path,required=True)
    ap.add_argument('--elf',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();manifest=json.loads(args.manifest.read_text())
    assert hashlib.sha256(args.elf.read_bytes()).hexdigest()==manifest['elf_sha256'],'ELF does not match capture package'
    content=args.log.read_text(errors='replace');assert content.startswith(manifest['log_header']),'Unexpected capture version'
    nm=Path(local_clang()).parent/'llvm-nm.exe'
    listing=subprocess.check_output([str(nm),str(args.elf)],text=True)
    symbols={int(p[0],16):p[2] for line in listing.splitlines() if len(p:=line.split())==3}
    report=parse(content,symbols)
    report.update(log_sha256=hashlib.sha256(args.log.read_bytes()).hexdigest(),elf_sha256=manifest['elf_sha256'],
        scope='Sparse instrumented original gameplay. Divide by sampled frames, not all window frames. Nested inclusive times and parallel worker/GPU times cannot be added.',
        unknown_functions=sorted({r['function'] for w in report['windows'] for r in w['samples'] if r['function'].startswith('unknown_')}))
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Complete windows:',len(report['windows']),'Rejected:',report['rejected'])
    for w in report['windows']:
        print('Window',w['id'],'sampled frames',w['sampled'],'instrumented frame ms',round(w['mean_instrumented_frame_ms'],2))
        for r in w['samples'][:8]:print(r['function'],r['context'],round(r['own_ms_per_sampled_render'],3),'exclusive ms/sample')
    assert report['windows'],'No complete trustworthy measurement window'


if __name__=='__main__':main()
