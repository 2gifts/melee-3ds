"""Fetch pinned public tools for local HOME Menu banner/CIA authoring."""
import argparse
import hashlib
import io
import json
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

from assets import ROOT

BASE = ROOT/'.toolchain/home-menu'
TOOLS = [
    ('makerom', 'https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-win_x86_64.zip',
     '88df4455e60556374e202d507f54ee03fac7493ec9554ab853157524b6d69db0', False),
    ('bannertool', 'https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip',
     'e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed', False),
    ('pycgfx', 'https://codeload.github.com/skyfloogle/pycgfx/zip/1f78850086f3a77c41e07162e842f97a5bf3c18a',
     'a822521761dbb428ee14df8d68340bd54595435a869fe835962f5b4d1c62c774', True),
]


def acquire(name, url, digest, strip_root):
    archive = BASE/(name+'.zip')
    if not archive.exists():
        request = urllib.request.Request(url, headers={'User-Agent': 'melee-3ds-local-builder'})
        with urllib.request.urlopen(request, timeout=90) as response:
            data = response.read()
        assert hashlib.sha256(data).hexdigest() == digest, name+' checksum mismatch'
        archive.write_bytes(data)
    data = archive.read_bytes()
    assert hashlib.sha256(data).hexdigest() == digest, name+' cached checksum mismatch'
    directory = (BASE/name).resolve()
    with zipfile.ZipFile(io.BytesIO(data)) as zipped:
        for entry in zipped.infolist():
            parts = Path(entry.filename).parts
            if strip_root:
                parts = parts[1:]
            if not parts or entry.is_dir():
                continue
            dest = directory.joinpath(*parts).resolve()
            assert dest.is_relative_to(directory), 'Unsafe archive path'
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(zipped.read(entry))
    print('Verified '+name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--skip-python', action='store_true', help='Python dependencies are already installed')
    args = ap.parse_args()
    BASE.mkdir(parents=True, exist_ok=True)
    for tool in TOOLS:
        acquire(*tool)
    if not args.skip_python:
        subprocess.run([sys.executable, '-m', 'pip', 'install', '--target', str(BASE/'python'),
                        'gltflib==1.0.13', 'dataclasses-json==0.6.7', 'marshmallow==3.26.2',
                        'typing-inspect==0.9.0', 'mypy-extensions==1.1.0'], check=True)
    (BASE/'tools-verified.json').write_text(json.dumps([
        dict(name=n, url=u, sha256=h) for n,u,h,_ in TOOLS], indent=2)+'\n')


if __name__ == '__main__':
    main()
