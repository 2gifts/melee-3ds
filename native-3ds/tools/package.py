"""Package the diagnostic and port source, without game data or toolchain binaries."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def validate_3dsx(path):
    data = path.read_bytes()
    if len(data) < 32:
        raise ValueError("Truncated 3DSX header")
    magic, header, reloc_header, version, flags, code, rodata, initialized, bss = \
        struct.unpack_from("<4sHH6I", data)
    if magic != b"3DSX" or header != 32 or reloc_header != 8 or version != 0 or flags != 0:
        raise ValueError("Unexpected 3DSX format")
    if code == 0 or initialized < bss:
        raise ValueError("Invalid 3DSX segment sizes")
    reloc_end = header + 3 * reloc_header
    if len(data) < reloc_end:
        raise ValueError("Truncated 3DSX relocation headers")
    relocations = struct.unpack_from("<6I", data, header)
    expected = reloc_end + code + rodata + initialized - bss + 4 * sum(relocations)
    if expected != len(data):
        raise ValueError(f"3DSX bounds mismatch: expected {expected}, found {len(data)}")
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "code_bytes": code, "rodata_bytes": rodata,
            "data_bytes_including_bss": initialized, "bss_bytes": bss,
            "relocation_records": sum(relocations)}


def main():
    binary = ROOT / "dist/3ds/melee/melee-diagnostics.3dsx"
    info = validate_3dsx(binary)
    output = ROOT / "dist/melee-3ds-foundation.zip"
    sources = [ROOT / p for p in ("README.md", ".gitignore", "upstream.lock.json",
                                  "toolchain.lock.json")]
    for directory in ("port", "tools", "tests", "docs"):
        sources += [p for p in (ROOT / directory).rglob("*") if p.is_file()
                    and "__pycache__" not in p.parts and p.suffix != ".pyc"]
    readme = """Melee 3DS port foundation - NOT A PLAYABLE GAME

Copy this ZIP's 3ds folder to the root of your SD card, then launch
melee-diagnostics in Homebrew Launcher. SELECT exits.
No game files are needed. Expect a green curve and a yellow Circle Pad
marker, with engine/input status on the bottom screen.

The diagnostic exercises Melee's original RNG and spline code. It does
not load fighters, stages, menus or gameplay. The full port is unfinished.
It has been tested in Azahar 2126.1, but not on a physical 3DS.

source/ contains build scripts, port code, tests and documentation.
Read source/README.md and source/docs/PORT_STATUS.md for the exact state.
Run the bootstrap script to fetch the pinned upstream source and dependencies.
Game data, the upstream checkout, toolchains and the unintegrated ARM archive
are excluded from this package. Original notices remain with dependencies.
"""
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr("README.txt", readme)
        bundle.writestr("diagnostic-build.json", json.dumps(info, indent=2) + "\n")
        bundle.write(binary, "3ds/melee/melee-diagnostics.3dsx")
        for path in sorted(sources):
            bundle.write(path, "source/" + path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(output) as bundle:
        failure = bundle.testzip()
        if failure:
            raise ValueError(f"ZIP verification failed: {failure}")
    print(json.dumps({"package": str(output), "package_bytes": output.stat().st_size,
                      "diagnostic": info}, indent=2))


if __name__ == "__main__":
    main()
