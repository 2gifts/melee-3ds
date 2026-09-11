"""Validate/extract a user-supplied Melee US 1.02 GameCube disc dump.

No downloads. Extracted bytes remain in their original GameCube format.
The manifest is an inventory, not a declaration that assets are ARM-ready.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "upstream.lock.json").read_text())


class AssetError(ValueError):
    pass


def u32(b, off=0):
    if off < 0 or off + 4 > len(b):
        raise AssetError("Truncated big-endian word")
    return struct.unpack_from(">I", b, off)[0]


def cstring(data, off):
    if not 0 <= off < len(data):
        raise AssetError("String offset outside string table")
    end = data.find(b"\0", off)
    if end == -1:
        raise AssetError("Unterminated string")
    try:
        return data[off:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise AssetError("Non-ASCII symbol or disc filename") from exc


def component(name):
    if (not name or name in (".", "..") or name[-1] in ". " or
        any(ord(c) < 32 or c in '/\\:<>"|?*' for c in name)):
        raise AssetError(f"Unsafe disc path component: {name!r}")
    reserved = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1,10)),
                *(f"LPT{i}" for i in range(1,10))}
    if name.split(".")[0].upper() in reserved:
        raise AssetError(f"Reserved filename: {name}")
    return name


@dataclass(frozen=True)
class Entry:
    path: str
    offset: int
    size: int
    directory: bool = False


def parse_fst(fst, iso_size):
    if len(fst) < 12 or fst[0] != 1 or u32(fst, 4) != 0:
        raise AssetError("Invalid GameCube FST root")
    count = u32(fst, 8)
    if count < 1 or count > len(fst) // 12:
        raise AssetError("FST count exceeds table bounds")
    names = fst[count * 12:]
    stack = [(count, PurePosixPath(), 0)]
    paths = set()
    entries = []
    for i in range(1, count):
        while i >= stack[-1][0]:
            stack.pop()
        word, offset, size = struct.unpack_from(">III", fst, i * 12)
        kind = word >> 24
        if kind not in (0, 1):
            raise AssetError("Invalid FST node type")
        name = component(cstring(names, word & 0xffffff))
        path = stack[-1][1] / name
        key = str(path).casefold()
        if key in paths:
            raise AssetError(f"Duplicate disc path: {path}")
        paths.add(key)
        if kind == 1:
            if offset != stack[-1][2] or not i < size <= stack[-1][0]:
                raise AssetError("Invalid directory parent/end index")
            stack.append((size, path, i))
        elif offset > iso_size or size > iso_size - offset:
            raise AssetError(f"Disc file outside image: {path}")
        entries.append(Entry(str(path), offset, size, bool(kind)))
    return entries


def hsd_inventory(data):
    """Inspect HSD metadata; this does not guess mixed field types or relocate."""
    if len(data) < 32 or u32(data) != len(data):
        raise AssetError("HSD archive size mismatch")
    size, reloc, public, external = (u32(data, n) for n in (4,8,12,16))
    reloc_base = 32 + size
    public_base = reloc_base + 4 * reloc
    extern_base = public_base + 8 * public
    strings_base = extern_base + 8 * external
    if strings_base > len(data):
        raise AssetError("HSD tables exceed file")
    body = memoryview(data)[32:32+size]
    for i in range(reloc):
        off = u32(data, reloc_base + i*4)
        if off % 4 or off > size - 4 or u32(body, off) >= size:
            raise AssetError("Invalid HSD relocation")
    names = data[strings_base:]
    roots, externs = [], []
    for n, base, dest in ((public, public_base, roots), (external, extern_base, externs)):
        for i in range(n):
            off, name = struct.unpack_from(">II", data, base+i*8)
            dest.append({"offset": off, "name": cstring(names, name)})
            if dest is roots and off >= size:
                raise AssetError("Public root outside HSD data")
            if dest is externs:
                visited = set()
                while off != 0xffffffff:
                    if off % 4 or off > size - 4 or off in visited:
                        raise AssetError("Invalid/cyclic HSD external chain")
                    visited.add(off)
                    off = u32(body, off)
    return {"data_size": size, "relocations": reloc, "roots": roots, "externals": externs}


def read_exact(f, offset, size):
    f.seek(offset)
    data = f.read(size)
    if len(data) != size:
        raise AssetError("Truncated disc image")
    return data


def dol_size(header):
    if len(header) < 256:
        raise AssetError("Truncated DOL header")
    end = 256
    for i in range(18):
        off, size = u32(header, i*4), u32(header, 0x90+i*4)
        if size:
            if off < 256 or off + size > 32*1024*1024:
                raise AssetError("Invalid DOL section")
            end = max(end, off+size)
    return end


def validate_dol(data):
    actual = hashlib.sha1(data).hexdigest()
    if actual != LOCK["main_dol_sha1"]:
        raise AssetError(f"main.dol is not the supported US 1.02 revision (SHA-1 {actual})")


def dol_region(data, address, size):
    for i in range(18):
        off, addr, length = u32(data,i*4), u32(data,0x48+i*4), u32(data,0x90+i*4)
        if addr <= address and address+size <= addr+length:
            start = off + address-addr
            if start + size > len(data):
                raise AssetError("DOL section exceeds input file")
            return data[start:start+size]
    raise AssetError(f"Address 0x{address:x} is outside the DOL sections")


def extract_fonts(dol, output):
    data = dol.read_bytes()
    validate_dol(data)
    # Sizes/addresses in the pinned decomp's GALE01 symbols.txt; element byte
    # arrays in hsd_3915.h and sislib_font.h require no host-endian conversion.
    for name,address,size,stride in (("debug_font",0x804088b8,0x1c00,56),
                                      ("sislib_font",0x8040cd40,0x23e00,512)):
        raw = dol_region(data,address,size)
        target = output / "sysdolphin/baselib" / (name+".inc")
        target.parent.mkdir(parents=True,exist_ok=True)
        with target.open("x") as f:
            for start in range(0,len(raw),stride):
                f.write("{{" + ",".join(f"0x{v:02x}" for v in raw[start:start+stride]) + "}},\n")


def disc_info(path):
    size = path.stat().st_size
    with path.open("rb") as f:
        h = read_exact(f, 0, 0x440)
        if h[:6] != b"GALE01" or h[6] != 0 or h[7] != 2 or u32(h, 0x1c) != 0xc2339f3d:
            raise AssetError("Expected uncompressed GALE01 US v1.02 ISO/GCM; convert RVZ in Dolphin first")
        dol, fst, fst_size = (u32(h,n) for n in (0x420,0x424,0x428))
        if fst_size < 12 or fst_size > 16*1024*1024 or fst+fst_size > size:
            raise AssetError("Invalid FST range")
        dh = read_exact(f, dol, 256)
        dsize = dol_size(dh)
        original_dol = read_exact(f, dol, dsize)
        validate_dol(original_dol)
        fst_data = read_exact(f, fst, fst_size)
        entries = parse_fst(fst_data, size)
    return entries, original_dol, fst_data


def inventory(path):
    if path.suffix.lower() in (".dat", ".usd"):
        try:
            return hsd_inventory(path.read_bytes())
        except AssetError as exc:
            return {"error": str(exc)}
    return None


def write_manifest(root, entries, dol_hash):
    manifest = {"game_id": "GALE01", "revision": 2, "main_dol_sha1": dol_hash,
                "format": "original-big-endian", "converted_for_arm": False, "files": entries}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def scan_extracted(root):
    dol = root / "sys/main.dol"
    files = root / "files"
    if not dol.is_file() or not files.is_dir():
        raise AssetError("Expected a Dolphin extraction with sys/main.dol and files/")
    validate_dol(dol.read_bytes())
    records = []
    for p in sorted(files.rglob("*")):
        if p.is_symlink() or not p.resolve().is_relative_to(files.resolve()):
            raise AssetError("Extracted directory contains a symlink or escaping path")
        if p.is_file():
            with p.open("rb") as f:
                digest = hashlib.file_digest(f, "sha256").hexdigest()
            records.append({"path": p.relative_to(files).as_posix(), "size": p.stat().st_size,
                            "sha256": digest, "hsd": inventory(p)})
    return {"game_id": "GALE01", "revision": 2, "main_dol_sha1": LOCK["main_dol_sha1"],
            "format": "original-big-endian", "converted_for_arm": False, "files": records}


def extract_iso(source, output):
    entries, dol, fst = disc_info(source)
    if output.exists():
        raise AssetError("Output already exists; choose a new directory (existing files are preserved)")
    output.mkdir(parents=True)
    root = output.resolve()
    files = root / "files"
    files.mkdir()
    (root / "sys").mkdir()
    (root / "sys/main.dol").write_bytes(dol)
    (root / "sys/fst.bin").write_bytes(fst)
    records = []
    with source.open("rb") as src:
        for entry in entries:
            dest = files.joinpath(*PurePosixPath(entry.path).parts)
            if not dest.resolve().is_relative_to(files):
                raise AssetError("Output path escapes extraction")
            if entry.directory:
                dest.mkdir()
                continue
            src.seek(entry.offset)
            remaining = entry.size
            digest = hashlib.sha256()
            with dest.open("xb") as dst:
                while remaining:
                    block = src.read(min(1024*1024, remaining))
                    if not block:
                        raise AssetError("Disc truncated during extraction")
                    dst.write(block)
                    digest.update(block)
                    remaining -= len(block)
            records.append({"path": entry.path, "size": entry.size,
                            "sha256": digest.hexdigest(), "hsd": inventory(dest)})
    return write_manifest(root, records, LOCK["main_dol_sha1"])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    subs = ap.add_subparsers(dest="command", required=True)
    p = subs.add_parser("extract"); p.add_argument("iso", type=Path); p.add_argument("output", type=Path)
    p = subs.add_parser("inspect"); p.add_argument("archive", type=Path)
    p = subs.add_parser("scan"); p.add_argument("root", type=Path); p.add_argument("--output", type=Path)
    p = subs.add_parser("fonts"); p.add_argument("dol", type=Path); p.add_argument("output", type=Path)
    args = ap.parse_args()
    try:
        if args.command == "extract":
            report = extract_iso(args.iso, args.output)
            print(f"Extracted and inventoried {len(report['files'])} files to {args.output}")
        elif args.command == "inspect":
            print(json.dumps(hsd_inventory(args.archive.read_bytes()), indent=2))
        elif args.command == "fonts":
            extract_fonts(args.dol,args.output)
            print(f"Extracted font include files to {args.output}")
        else:
            report = scan_extracted(args.root)
            if args.output:
                with args.output.open("x") as f:
                    json.dump(report, f, indent=2)
            else:
                print(json.dumps(report, indent=2))
    except (AssetError, OSError) as exc:
        ap.exit(1, f"Asset error: {exc}\n")


if __name__ == "__main__":
    main()
