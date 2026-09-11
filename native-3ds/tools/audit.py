"""Audit upstream ARM compilation. Passing syntax does not imply a working port."""
import argparse
import collections
import concurrent.futures
import json
import re
import subprocess
from pathlib import Path
from build import ROOT, UPSTREAM, prepare, local_clang, common_flags


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--objects", action="store_true",
                    help="Compile real ARM objects for gameplay/HSD units and archive those that pass")
    args = ap.parse_args()
    cc = local_clang()
    sdk = ROOT / ".toolchain/devkitpro/devkitARM/arm-none-eabi/include"
    flags = [cc, "--no-default-config", "--target=arm-none-eabi", "-mcpu=mpcore",
             "-mfpu=vfp", "-mfloat-abi=hard", "-std=gnu11", "-fsyntax-only",
             "-ferror-limit=5", "-Wno-everything", "-DMP_GAME_ABI", "-fno-short-enums", *prepare(),
             "-isystem", str(sdk)]
    sources = sorted((UPSTREAM / "src").rglob("*.c"))
    sources += sorted((UPSTREAM / "extern/dolphin/src").rglob("*.c"))
    object_root = ROOT / "build/arm-objects"
    if args.objects:
        sources = [p for p in sources if p.is_relative_to(UPSTREAM / "src/melee")
                   or p.is_relative_to(UPSTREAM / "src/sysdolphin")]
        flags.remove("-fsyntax-only")
        flags += common_flags() + ["-Wno-everything", "-c"]
        object_root.mkdir(parents=True, exist_ok=True)

    def check(path):
        obj = object_root / (path.relative_to(UPSTREAM).as_posix().replace("/", "__") + ".o")
        command = [*flags, str(path)] + (["-o", str(obj)] if args.objects else [])
        p = subprocess.run(command, capture_output=True, text=True,
                           errors="replace", timeout=60)
        errors = re.findall(r"(?:fatal )?error: (.+)", p.stderr)
        return {"file": path.relative_to(UPSTREAM).as_posix(),
                "syntax_ok": p.returncode == 0, "errors": errors,
                "diagnostic": p.stderr.replace(str(ROOT), "<workspace>")[:10000]}

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(check, sources))
    counts = collections.Counter()
    for r in results:
        counts.update(r["errors"][:1])
    report = {"upstream_commit": json.loads((ROOT / "upstream.lock.json").read_text())["commit"],
              "target": "ARMv6K / 32-bit / little-endian / " + ("object compilation" if args.objects else "syntax only"),
              "total": len(results), "syntax_pass": sum(r["syntax_ok"] for r in results),
              "first_error_counts": counts.most_common(), "files": results}
    out = ROOT / "build/audit"
    out.mkdir(parents=True, exist_ok=True)
    if args.objects:
        archive = object_root / "libmelee-unintegrated.a"
        if archive.exists():
            archive.unlink()
        ar = Path(cc).with_name("llvm-ar.exe" if Path(cc).suffix == ".exe" else "llvm-ar")
        # Response files avoid Windows command-line size limits.
        objects = [object_root / (r["file"].replace("/", "__") + ".o")
                   for r in results if r["syntax_ok"]]
        rsp = object_root / "objects.rsp"
        rsp.write_text("\n".join('"'+p.as_posix()+'"' for p in objects))
        subprocess.run([str(ar), "rcs", str(archive), "@"+str(rsp)], check=True)
        nm = Path(cc).with_name("llvm-nm.exe" if Path(cc).suffix == ".exe" else "llvm-nm")
        symbols = subprocess.check_output([str(nm), "--format=posix", str(archive)], text=True)
        defined, required = set(), set()
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) > 1:
                if parts[1] == "U": required.add(parts[0])
                elif parts[1].isupper(): defined.add(parts[0])
        (out / "unresolved-symbols.txt").write_text("\n".join(sorted(required-defined))+"\n")
        report["unresolved_symbols"] = len(required-defined)
    (out / ("arm-objects.json" if args.objects else "arm-syntax.json")).write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k,v in report.items() if k != "files"}, indent=2))


if __name__ == "__main__":
    main()
