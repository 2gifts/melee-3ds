"""Read-only verification of the supplied ISO's extracted assets on an SD card."""
import argparse,hashlib,json,time
from pathlib import Path
from build import ROOT

def main():
    ap=argparse.ArgumentParser();ap.add_argument('files',type=Path);args=ap.parse_args()
    root=args.files.resolve();manifest=json.loads((ROOT/'assets/GALE01/manifest.json').read_text())
    count=total=0;bad=[];started=time.monotonic()
    for entry in manifest['files']:
        path=(root/entry['path']).resolve();assert path.is_relative_to(root)
        if not path.is_file() or path.stat().st_size!=entry['size']:bad.append(entry['path'])
        else:
            with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
            if digest!=entry['sha256']:bad.append(entry['path'])
        count+=1;total+=entry['size']
        if count%100==0:print(f'Checked {count}/{len(manifest["files"])} files; {len(bad)} mismatches',flush=True)
    result={'files_root':str(root),'checked_files':count,'expected_bytes':total,'mismatches':bad,'seconds':time.monotonic()-started}
    (ROOT/'build/hardware/2026-09-10-startup/sd-assets-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True);assert not bad,bad
if __name__=='__main__':main()
