import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import assets


def fst(entries, names):
    return b"".join(struct.pack(">III", *e) for e in entries) + names


class AssetTests(unittest.TestCase):
    def test_nested_fst(self):
        table = fst([(0x1000000,0,4), (0x1000000,0,3),
                     (4,0x1000,16), (6,0x1010,2)], b"dir\0a\0b\0")
        entries = assets.parse_fst(table, 0x2000)
        self.assertEqual([e.path for e in entries], ["dir", "dir/a", "b"])

    def test_bad_paths_and_bounds(self):
        for name in ("..", "/abs", "a/b", "a\\b", "C:foo", "NUL", "x.", "x "):
            with self.subTest(name=name), self.assertRaises(assets.AssetError):
                assets.parse_fst(fst([(0x1000000,0,2),(0,100,4)], name.encode()+b"\0"), 200)
        for table in (fst([(0x1000000,0,9)], b""),
                      fst([(0x1000000,0,2),(0,199,4)], b"x\0"),
                      fst([(0x1000000,0,2),(0,10,4)], b"x"),
                      fst([(0x1000000,0,2),(0x1000000,0,3)], b"x\0"),
                      fst([(0x1000000,0,3),(0,10,4),(0,14,4)], b"x\0")):
            with self.assertRaises(assets.AssetError):
                assets.parse_fst(table, 200)

    def test_hsd(self):
        data = struct.pack(">8I", 65,16,1,1,0,0,0,0)
        data += struct.pack(">4I",0,0x3fc00000,0,0)
        data += struct.pack(">3I",0,4,0) + b"root\0"
        result = assets.hsd_inventory(data)
        self.assertEqual(result["roots"], [{"offset":4,"name":"root"}])
        for n in range(len(data)):
            with self.assertRaises(assets.AssetError):
                assets.hsd_inventory(data[:n])
        with self.assertRaises(assets.AssetError):
            assets.hsd_inventory(data[:-1]+b"x")

    def test_external_cycle(self):
        data = struct.pack(">8I",46,4,0,0,1,0,0,0)
        data += struct.pack(">3I",0,0,0)+b"e\0"
        with self.assertRaises(assets.AssetError):
            assets.hsd_inventory(data)
        data = data[:32] + b"\xff"*4 + data[36:]
        self.assertEqual(len(assets.hsd_inventory(data)["externals"]), 1)

    def test_synthetic_disc_extraction(self):
        # Only the expected digest is substituted; production revision checks
        # are never bypassed by an extractor flag.
        image = bytearray(0x1100)
        image[:8] = b"GALE01\x00\x02"
        struct.pack_into(">I", image, 0x1c, 0xc2339f3d)
        dol = bytearray(260)
        struct.pack_into(">I", dol, 0, 256)
        struct.pack_into(">I", dol, 0x90, 4)
        dol[256:] = b"TEST"
        image[0x500:0x500+len(dol)] = dol
        table = fst([(0x1000000,0,2),(0,0x1000,4)], b"test.bin\0")
        image[0x800:0x800+len(table)] = table
        struct.pack_into(">III",image,0x420,0x500,0x800,len(table))
        image[0x1000:0x1004] = b"DATA"
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); iso = p/"test.iso"; iso.write_bytes(image)
            with self.assertRaises(assets.AssetError):
                assets.disc_info(iso)
            with patch.dict(assets.LOCK, {"main_dol_sha1":hashlib.sha1(dol).hexdigest()}):
                report = assets.extract_iso(iso,p/"extracted")
                self.assertEqual((p/"extracted/files/test.bin").read_bytes(),b"DATA")
                self.assertFalse(report["converted_for_arm"])
                self.assertEqual(assets.scan_extracted(p/"extracted")["files"],report["files"])
                with self.assertRaises(assets.AssetError):
                    assets.extract_iso(iso,p/"extracted")

    def test_dol_region_mapping(self):
        data = bytearray(272)
        struct.pack_into(">I",data,0,256)
        struct.pack_into(">I",data,0x48,0x80004000)
        struct.pack_into(">I",data,0x90,16)
        data[256:] = bytes(range(16))
        self.assertEqual(assets.dol_region(data,0x80004004,4),bytes(range(4,8)))
        with self.assertRaises(assets.AssetError):
            assets.dol_region(data,0x80003ffc,4)
        with self.assertRaises(assets.AssetError):
            assets.dol_region(data,0x8000400c,8)


if __name__ == "__main__":
    unittest.main()
