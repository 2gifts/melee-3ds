"""Fetch pinned source; optionally set up the tested portable Windows toolchain.

Normal builds use an installed devkitPro 3ds-dev toolchain. --portable-windows
downloads LLVM plus SDK libraries from the official devkitPro Docker image.
All downloads and extracted files stay inside this workspace.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True)


def checkout(url, commit, path):
    if path.exists():
        rev = subprocess.check_output(["git","-C",str(path),"rev-parse","HEAD"],text=True).strip()
        if rev != commit:
            raise RuntimeError(f"Existing {path} has revision {rev}; preserving it")
        return
    run("git","clone","--no-checkout","--filter=blob:none",url,path)
    run("git","-C",path,"checkout","--detach",commit)


def download(url, dest, sha256, headers=None):
    if dest.exists():
        with dest.open("rb") as f:
            if hashlib.file_digest(f,"sha256").hexdigest() == sha256.lower():
                return
        raise RuntimeError(f"Existing download has wrong checksum: {dest}")
    dest.parent.mkdir(parents=True,exist_ok=True)
    tmp = dest.with_suffix(dest.suffix+".partial")
    request = urllib.request.Request(url,headers=headers or {})
    with urllib.request.urlopen(request,timeout=60) as src, tmp.open("wb") as dst:
        shutil.copyfileobj(src,dst,1024*1024)
    with tmp.open("rb") as f:
        if hashlib.file_digest(f,"sha256").hexdigest() != sha256.lower():
            raise RuntimeError(f"Download checksum mismatch: {dest.name}")
    tmp.replace(dest)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--portable-windows",action="store_true")
    args = ap.parse_args()
    lock = json.loads((ROOT/"upstream.lock.json").read_text())
    checkout(lock["repository"],lock["commit"],ROOT/"upstream/melee")
    if not args.portable_windows:
        print("Pinned Melee source ready. Use devkitPro 3ds-dev to build.")
        return
    if os.name != "nt":
        ap.error("Use an installed devkitPro toolchain on Linux/macOS")
    lock = json.loads((ROOT/"toolchain.lock.json").read_text())
    local = ROOT/".toolchain"
    archive = local/"downloads/llvm-mingw.zip"
    download(lock["llvm_url"],archive,lock["llvm_sha256"])
    compiler = local/lock["llvm_directory"]/"bin/clang.exe"
    if not compiler.exists():
        with zipfile.ZipFile(archive) as z:
            for item in z.infolist():
                dest = (local/item.filename).resolve()
                if not dest.is_relative_to(local.resolve()):
                    raise RuntimeError("Zip path escapes toolchain directory")
            z.extractall(local)
    layer = local/"downloads/devkitarm-layer.tar.gz"
    if not layer.exists():
        request = "https://auth.docker.io/token?service=registry.docker.io&scope=repository:devkitpro/devkitarm:pull"
        with urllib.request.urlopen(request,timeout=30) as response:
            token = json.load(response)["token"]
        download("https://registry-1.docker.io/v2/devkitpro/devkitarm/blobs/"+lock["sdk_layer"],
                 layer,lock["sdk_layer"].split(":")[1],{"Authorization":"Bearer "+token})
    run(os.sys.executable,ROOT/"tools/extract_sdk.py",layer,local/"devkitpro")
    source = ROOT/"references/3dstools"
    checkout(lock["3dstools_repository"],lock["3dstools_commit"],source)
    (local/"bin").mkdir(exist_ok=True)
    run(compiler.with_name("clang++.exe"),"-O2","-static",
        source/"src/3dsxtool.cpp",source/"src/romfs.cpp","-o",local/"bin/3dsxtool.exe")
    shader_source = ROOT/"references/picasso"
    checkout(lock["picasso_repository"],lock["picasso_commit"],shader_source)
    run(compiler.with_name("clang++.exe"),"-O2","-static",
        '-DPACKAGE_STRING="picasso '+lock["picasso_commit"][:8]+'"',
        shader_source/"source/picasso_assembler.cpp",
        shader_source/"source/picasso_frontend.cpp","-o",local/"bin/picasso.exe")
    print("Portable toolchain ready. Extract your GALE01 v1.02 assets and fonts, then run python tools/build_game.py --release")


if __name__ == "__main__":
    main()
