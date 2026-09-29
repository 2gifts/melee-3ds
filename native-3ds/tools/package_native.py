"""Stage a local SD-card package using the user's extracted game assets.

--link hard-links the game files to the extracted ones when they are on the
same drive, instead of copying 1.4 GB (tools/easy_build.py)."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import sys
LINK='--link' in sys.argv[1:]
from package import validate_3dsx
ROOT=Path(__file__).resolve().parents[1]
package=ROOT/'dist/native-alpha'
# Either or both save profiles: melee.3dsx (everything unlocked) and
# melee-fresh.3dsx (fresh save, built with --profile fresh).
binaries=[package/'3ds/melee'/name for name in ('melee.3dsx','melee-fresh.3dsx') if (package/'3ds/melee'/name).is_file()]
if not binaries:raise SystemExit('Build with tools/build_game.py --release [--profile fresh] first')
source=ROOT/'assets/GALE01/files';dest=package/'3ds/melee/files'
manifest=json.loads((source.parent/'manifest.json').read_text())
binary_info={b.name:validate_3dsx(b) for b in binaries}
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
# Top-level files go in small folders; one 1000-entry folder makes every
# file open slow on the console (tools/sd_layout.py).
from sd_layout import locations
layout=locations([entry['path'] for entry in manifest['files']])
count=0;total=0
for entry in manifest['files']:
    relative=Path(entry['path']);path=source/relative;target=dest/layout[entry['path']]
    if relative.is_absolute() or '..' in relative.parts:raise ValueError('Invalid manifest path')
    target.parent.mkdir(parents=True,exist_ok=True);size=entry['size']
    flat=dest/relative
    if flat!=target and flat.is_file():  # staged by an older package: move it
        if target.is_file():flat.unlink()
        else:flat.rename(target)
    if not target.is_file() or target.stat().st_size!=size or digest(target)!=entry['sha256']:
        if path.stat().st_size!=size or digest(path)!=entry['sha256']:raise ValueError(f'Extracted asset hash mismatch: {relative}')
        target.unlink(missing_ok=True)
        try:
            if not LINK:raise OSError
            os.link(path,target)  # --link: share the extracted file's data (same drive)
        except OSError:shutil.copy2(path,target)
        if digest(target)!=entry['sha256']:raise ValueError(f'Packaged asset hash mismatch: {relative}')
    count+=1;total+=size
report={'built_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'binaries':binary_info,'upstream_commit':json.loads((ROOT/'upstream.lock.json').read_text())['commit'],
        'game_id':manifest['game_id'],'main_dol_sha1':manifest['main_dol_sha1'],
        'verified_asset_files':count,'verified_asset_bytes':total,
        'physical_hardware_verified':False,'status':'Native engine alpha; performance and hardware validation in progress'}
(package/'build.json').write_text(json.dumps(report,indent=2)+'\n')
for source,name in [(ROOT/'docs/THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
                    (ROOT/'port/3ds/vendor/CITRO3D-LICENSE.txt','CITRO3D-LICENSE.txt'),
                    (ROOT/'port/engine/vendor/DOLPHIN-LICENSE.txt','DOLPHIN-LICENSE.txt')]:
    shutil.copy2(source,package/name)
print(f'Verified {", ".join(b.name for b in binaries)} and {count} game files ({total:,} bytes) in {package}')
