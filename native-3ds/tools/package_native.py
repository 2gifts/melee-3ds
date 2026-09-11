"""Stage a local SD-card package using the user's extracted game assets."""
from pathlib import Path
import datetime
import hashlib
import json
import shutil
from package import validate_3dsx
ROOT=Path(__file__).resolve().parents[1]
package=ROOT/'dist/native-alpha'
binary=package/'3ds/melee/melee.3dsx'
if not binary.is_file():raise SystemExit('Build with tools/build_game.py --release first')
source=ROOT/'assets/GALE01/files';dest=package/'3ds/melee/files'
manifest=json.loads((source.parent/'manifest.json').read_text())
binary_info=validate_3dsx(binary)
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
count=0;total=0
for entry in manifest['files']:
    relative=Path(entry['path']);path=source/relative;target=dest/relative
    if relative.is_absolute() or '..' in relative.parts:raise ValueError('Invalid manifest path')
    target.parent.mkdir(parents=True,exist_ok=True);size=entry['size']
    if not target.is_file() or target.stat().st_size!=size or digest(target)!=entry['sha256']:
        if path.stat().st_size!=size or digest(path)!=entry['sha256']:raise ValueError(f'Extracted asset hash mismatch: {relative}')
        shutil.copy2(path,target)
        if digest(target)!=entry['sha256']:raise ValueError(f'Packaged asset hash mismatch: {relative}')
    count+=1;total+=size
report={'built_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'binary':binary_info,'upstream_commit':json.loads((ROOT/'upstream.lock.json').read_text())['commit'],
        'game_id':manifest['game_id'],'main_dol_sha1':manifest['main_dol_sha1'],
        'verified_asset_files':count,'verified_asset_bytes':total,
        'physical_hardware_verified':False,'status':'Native engine alpha; performance and hardware validation in progress'}
(package/'build.json').write_text(json.dumps(report,indent=2)+'\n')
for source,name in [(ROOT/'docs/THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
                    (ROOT/'port/3ds/vendor/CITRO3D-LICENSE.txt','CITRO3D-LICENSE.txt'),
                    (ROOT/'port/engine/vendor/DOLPHIN-LICENSE.txt','DOLPHIN-LICENSE.txt')]:
    shutil.copy2(source,package/name)
print(f'Verified native 3DSX and {count} game files ({total:,} bytes) in {package}')
