"""Extract ARM libraries/headers from an official devkitPro container layer.

This is a local Clang validation SDK, not a devkitPro installation. Linux
executables are deliberately excluded. Use devkitPro 3ds-dev for normal builds.
"""
import argparse
import hashlib
import tarfile
from pathlib import Path, PurePosixPath

LAYER_SHA256 = "3c5026e520aeec9e208399f6d0a62098d5ee4c13d8cca97cdb9adbc6bb56a1c0"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("layer", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    with args.layer.open("rb") as f:
        if hashlib.file_digest(f, "sha256").hexdigest() != LAYER_SHA256:
            ap.error("SDK layer does not match the pinned official image")
    root = args.output.resolve()
    count = 0
    with tarfile.open(args.layer, "r:gz") as tar:
        for member in tar:
            p = PurePosixPath(member.name)
            if not member.isfile() or p.parts[:2] != ("opt", "devkitpro"):
                continue
            rel = Path(*p.parts[2:])
            if not (rel.suffix in (".h", ".a", ".o", ".ld", ".specs")
                    or rel.name in ("3ds_rules", "base_rules")):
                continue
            target = (root / rel).resolve()
            if not target.is_relative_to(root):
                raise ValueError("Archive path escapes SDK directory")
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as src, target.open("wb") as dst:
                import shutil
                shutil.copyfileobj(src, dst)
            count += 1
    print(f"Extracted {count} SDK headers, libraries and linker inputs to {root}")


if __name__ == "__main__":
    main()
