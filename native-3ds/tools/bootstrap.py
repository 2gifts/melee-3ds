"""Fetch pinned source; optionally set up the tested portable Windows toolchain.

Normal builds use an installed devkitPro 3ds-dev toolchain. --portable-windows
downloads LLVM plus SDK libraries from the official devkitPro Docker image.
All downloads and extracted files stay inside this workspace.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import subprocess
import tarfile
import zipfile

from fetch import fetch, fetch_to

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True)


def checkout(url, commit, path):
    if path.exists():
        # A folder copied in by hand is not a checkout (and git would report
        # this repository's revision for it).
        if not (path/".git").exists():
            raise RuntimeError(f"Existing {path} is not a git checkout; delete it and run bootstrap again")
        rev = subprocess.check_output(["git","-C",str(path),"rev-parse","HEAD"],text=True).strip()
        if rev != commit:
            raise RuntimeError(f"Existing {path} has revision {rev}, not the pinned {commit}; "
                               "delete it and run bootstrap again (it was left unchanged)")
        return
    run("git","clone","--no-checkout","--filter=blob:none",url,path)
    run("git","-C",path,"checkout","--detach",commit)


def download(url, dest, sha256, headers=None):
    if dest.exists():
        with dest.open("rb") as f:
            if hashlib.file_digest(f,"sha256").hexdigest() == sha256.lower():
                return
        # Damaged (an interrupted copy, a disk error): fetch it again.
        print(f"Downloading {dest.name} again: the saved copy is damaged")
        dest.unlink()
    dest.parent.mkdir(parents=True,exist_ok=True)
    tmp = dest.with_suffix(dest.suffix+".partial")
    fetch_to(url,tmp,headers,timeout=60)
    with tmp.open("rb") as f:
        if hashlib.file_digest(f,"sha256").hexdigest() != sha256.lower():
            tmp.unlink()
            raise RuntimeError(f"Download checksum mismatch: {dest.name}")
    tmp.replace(dest)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--portable-windows",action="store_true")
    ap.add_argument("--portable-linux",action="store_true",
                    help="Same pinned toolchain for Linux (used by the Docker builder; needs g++)")
    args = ap.parse_args()
    lock = json.loads((ROOT/"upstream.lock.json").read_text())
    checkout(lock["repository"],lock["commit"],ROOT/"upstream/melee")
    # Release builds link an adapted copy of this exact Citro3D render queue
    # (tools/async_renderqueue.py checks the source's hash).
    tools = json.loads((ROOT/"toolchain.lock.json").read_text())
    checkout(tools["citro3d_repository"],tools["citro3d_commit"],ROOT/"references/citro3d")
    if not (args.portable_windows or args.portable_linux):
        print("Pinned Melee source ready. Use devkitPro 3ds-dev to build.")
        return
    if args.portable_windows and os.name != "nt":
        ap.error("--portable-windows only works on Windows; use --portable-linux in Linux/Docker")
    if args.portable_linux and os.name == "nt":
        ap.error("--portable-linux does not work on Windows; use --portable-windows")
    lock = json.loads((ROOT/"toolchain.lock.json").read_text())
    local = ROOT/".toolchain"
    exe = ".exe" if os.name == "nt" else ""
    if os.name == "nt":
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
    else:
        arch = {"arm64":"aarch64","aarch64":"aarch64","x86_64":"x86_64","amd64":"x86_64"}.get(platform.machine().lower())
        if arch is None:
            ap.error("Unsupported CPU for the pinned Linux LLVM: "+platform.machine())
        llvm = lock["llvm_linux"][arch]
        archive = local/"downloads"/llvm["url"].rsplit("/",1)[-1]
        download(llvm["url"],archive,llvm["sha256"])
        compiler = local/llvm["directory"]/"bin/clang"
        if not compiler.exists():
            with tarfile.open(archive,"r:xz") as t:
                t.extractall(local,filter="data")
    layer = local/"downloads/devkitarm-layer.tar.gz"
    if not layer.exists():
        request = "https://auth.docker.io/token?service=registry.docker.io&scope=repository:devkitpro/devkitarm:pull"
        token = json.loads(fetch(request,timeout=30))["token"]
        download("https://registry-1.docker.io/v2/devkitpro/devkitarm/blobs/"+lock["sdk_layer"],
                 layer,lock["sdk_layer"].split(":")[1],{"Authorization":"Bearer "+token})
    run(os.sys.executable,ROOT/"tools/extract_sdk.py",layer,local/"devkitpro")
    source = ROOT/"references/3dstools"
    checkout(lock["3dstools_repository"],lock["3dstools_commit"],source)
    (local/"bin").mkdir(exist_ok=True)
    cxx = compiler.with_name("clang++.exe") if os.name == "nt" else "g++"
    run(cxx,"-O2","-static",
        source/"src/3dsxtool.cpp",source/"src/romfs.cpp","-o",local/"bin"/("3dsxtool"+exe))
    shader_source = ROOT/"references/picasso"
    checkout(lock["picasso_repository"],lock["picasso_commit"],shader_source)
    run(cxx,"-O2","-static",
        '-DPACKAGE_STRING="picasso '+lock["picasso_commit"][:8]+'"',
        shader_source/"source/picasso_assembler.cpp",
        shader_source/"source/picasso_frontend.cpp","-o",local/"bin"/("picasso"+exe))
    print("Portable toolchain ready. Extract your GALE01 v1.02 assets and fonts, then run python tools/build_game.py --release")


if __name__ == "__main__":
    main()
