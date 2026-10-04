"""Fetch pinned public tools for local HOME Menu banner/CIA authoring."""
import argparse
import hashlib
import io
import json
import zipfile
from pathlib import Path

from assets import ROOT
from fetch import fetch as download

BASE = ROOT/'.toolchain/home-menu'
TOOLS = [
    ('makerom', 'https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-win_x86_64.zip',
     '88df4455e60556374e202d507f54ee03fac7493ec9554ab853157524b6d69db0', False),
    ('bannertool', 'https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip',
     'e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed', False),
    ('pycgfx', 'https://codeload.github.com/skyfloogle/pycgfx/zip/1f78850086f3a77c41e07162e842f97a5bf3c18a',
     'a822521761dbb428ee14df8d68340bd54595435a869fe835962f5b4d1c62c774', True),
]
# pycgfx's pure-Python dependencies, as pinned wheels (no pip needed).
WHEELS = [
    ('https://files.pythonhosted.org/packages/e3/81/1b0a6be6d15c657f47c2b4fea4a7826aad4271a4f3a3fb41c8d6ce70f24f/'
     'gltflib-1.0.13-py3-none-any.whl', '8a09ac0159bf7b957c04e634e500e099ac2765a55025f531d6a2bd546b8a02d4'),
    ('https://files.pythonhosted.org/packages/c3/be/d0d44e092656fe7a06b55e6103cbce807cdbdee17884a5367c68c9860853/'
     'dataclasses_json-0.6.7-py3-none-any.whl', '0dbf33f26c8d5305befd61b39d2b3414e8a407bedc2834dea9b8d642666fb40a'),
    ('https://files.pythonhosted.org/packages/be/2f/5108cb3ee4ba6501748c4908b908e55f42a5b66245b4cfe0c99326e1ef6e/'
     'marshmallow-3.26.2-py3-none-any.whl', '013fa8a3c4c276c24d26d84ce934dc964e2aa794345a0f8c7e5a7191482c8a73'),
    ('https://files.pythonhosted.org/packages/65/f3/107a22063bf27bdccf2024833d3445f4eea42b2e598abfbd46f6a63b6cb0/'
     'typing_inspect-0.9.0-py3-none-any.whl', '9ee6fc59062311ef8547596ab6b955e1b8aa46242d854bfc78f4f6b0eff35f9f'),
    ('https://files.pythonhosted.org/packages/79/7b/2c79738432f5c924bef5071f933bcc9efd0473bac3b4aa584a6f7c1c8df8/'
     'mypy_extensions-1.1.0-py3-none-any.whl', '1be4cccdb0f2482337c4743e60421de3a356cd97508abadd57d47403e94f5505'),
    ('https://files.pythonhosted.org/packages/18/67/36e9267722cc04a6b9f15c7f3441c2363321a3ea07da7ae0c0707beb2a9c/'
     'typing_extensions-4.15.0-py3-none-any.whl', 'f0fa19c6845758ab08074a0cfa8b7aecb71c999ca73d62883bc25cc018c4e548'),
    ('https://files.pythonhosted.org/packages/20/12/38679034af332785aac8774540895e234f4d07f7545804097de4b666afd8/'
     'packaging-25.0-py3-none-any.whl', '29572ef2b1f17581046b3a2227d5c611fb25ec70ca1ba8554b24b0e69331a484'),
]


def fetch(url, digest, archive):
    if archive.exists():
        data = archive.read_bytes()
        if hashlib.sha256(data).hexdigest() == digest:
            return data
        # Damaged (an interrupted copy, a disk error): fetch it again.
        print(f'Downloading {archive.name} again: the saved copy is damaged')
        archive.unlink()
    data = download(url, {'User-Agent': 'melee-3ds-local-builder'}, timeout=90)
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError(archive.name+' checksum mismatch')
    archive.write_bytes(data)
    return data


def install_wheels():
    """Unpack the pinned wheels into .toolchain/home-menu/python."""
    target = (BASE/'python').resolve()
    (BASE/'wheels').mkdir(parents=True, exist_ok=True)
    for url, digest in WHEELS:
        name = url.rsplit('/', 1)[-1]
        data = fetch(url, digest, BASE/'wheels'/name)
        with zipfile.ZipFile(io.BytesIO(data)) as zipped:
            for entry in zipped.infolist():
                dest = target.joinpath(*Path(entry.filename).parts).resolve()
                assert dest.is_relative_to(target), 'Unsafe archive path'
                if not entry.is_dir():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(zipped.read(entry))
        print('Verified '+name)


def acquire(name, url, digest, strip_root):
    data = fetch(url, digest, BASE/(name+'.zip'))
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
    group = ap.add_mutually_exclusive_group()
    group.add_argument('--cia-only', action='store_true',
                       help='Only makerom and bannertool (CIA packaging with the generated 2D banner)')
    group.add_argument('--diorama-only', action='store_true',
                       help='Only pycgfx and its packages (the 3D banner made from the disc)')
    args = ap.parse_args()
    BASE.mkdir(parents=True, exist_ok=True)
    # pycgfx and its Python packages only make the 3D diorama. Its GitHub
    # source archive is also not guaranteed to keep the pinned checksum, so
    # the easy builder fetches them separately and falls back to 2D.
    tools = [t for t in TOOLS if (t[0] == 'pycgfx') == args.diorama_only or not (args.cia_only or args.diorama_only)]
    for tool in tools:
        acquire(*tool)
    if not args.skip_python and not args.cia_only:
        install_wheels()
    (BASE/'tools-verified.json').write_text(json.dumps([
        dict(name=n, url=u, sha256=h) for n,u,h,_ in TOOLS], indent=2)+'\n')


if __name__ == '__main__':
    main()
