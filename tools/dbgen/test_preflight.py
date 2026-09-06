import binascii
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
GEN = HERE / "nearby_dbgen.py"
PREFLIGHT = HERE / "preflight.py"
DOC = HERE.parent.parent / "docs" / "db" / "header-and-install.md"

HA = '''from typing import Final
BLUETOOTH: Final[list[dict]] = [
 {"domain":"demo","manufacturer_id":123},
 {"domain":"demo2","local_name":"NB-*","connectable":False},
]
'''
ZHA = '''MODELS_INFO="models_info"
MFR="NearBy"
class Demo:
 signature={MODELS_INFO:[(MFR,"ZB-1"),("Acme","ZB-2")]}
QuirkBuilder("BuilderCo", "QB-1")
'''

HEADER_SIZE = 128


def fix_header_crc(data: bytearray) -> None:
    data[76:80] = b"\0\0\0\0"
    crc = binascii.crc32(data[:HEADER_SIZE]) & 0xFFFFFFFF
    data[76:80] = struct.pack("<I", crc)


class WholeSdPreflightTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        td = Path(self.tmp.name)
        (td / "ha.py").write_text(HA)
        zha = td / "zha"
        zha.mkdir()
        (zha / "demo.py").write_text(ZHA)
        self.db = td / "test.nbdb"
        subprocess.run([
            sys.executable, str(GEN), "build",
            "--ha-bluetooth", str(td / "ha.py"), "--ha-rev", "ha-test",
            "--zha-root", str(zha), "--zha-rev", "zha-test",
            "--build-epoch", "1", "--out", str(self.db),
        ], check=True, capture_output=True)
        self.original = self.db.read_bytes()

    def tearDown(self):
        self.tmp.cleanup()

    def run_preflight(self, *, release=True):
        cmd = [sys.executable, str(PREFLIGHT), str(self.db)]
        if release:
            cmd.append("--release")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        return proc, json.loads(proc.stdout)

    def assert_rejected(self, expected_error):
        proc, result = self.run_preflight()
        self.assertEqual(proc.returncode, 2)
        self.assertFalse(result["safe_to_format"])
        self.assertIn(expected_error, "\n".join(result["errors"]))

    def test_valid_source_allows_confirmation_but_declares_destructive_model(self):
        proc, result = self.run_preflight()
        self.assertEqual(proc.returncode, 0)
        self.assertTrue(result["safe_to_format"])
        self.assertTrue(result["destructive"])
        self.assertEqual(result["install_model"], "whole_sd_destructive_v0_1")
        self.assertEqual(result["active_path"], "/nearby/db/nearby.nbdb")
        self.assertEqual(result["temporary_path"], "/nearby/db/nearby.nbdb.part")
        self.assertEqual(result["post_format_failure"], "no_usable_db_guaranteed")

    def test_truncated_source_is_structured_rejection(self):
        self.db.write_bytes(self.original[:64])
        self.assert_rejected("file shorter than 128-byte header")

    def test_bad_magic_is_rejected(self):
        data = bytearray(self.original)
        data[0] ^= 0x01
        self.db.write_bytes(data)
        self.assert_rejected("bad magic")

    def test_bad_header_crc_is_rejected(self):
        data = bytearray(self.original)
        data[20] ^= 0x01
        self.db.write_bytes(data)
        self.assert_rejected("header CRC32 mismatch")

    def test_incompatible_schema_is_rejected_even_with_valid_header_crc(self):
        data = bytearray(self.original)
        data[16:20] = struct.pack("<I", 999)
        fix_header_crc(data)
        self.db.write_bytes(data)
        self.assert_rejected("unsupported schema_version")

    def test_newer_reader_abi_is_rejected_even_with_valid_header_crc(self):
        data = bytearray(self.original)
        data[24:28] = struct.pack("<I", 999)
        fix_header_crc(data)
        self.db.write_bytes(data)
        self.assert_rejected("reader ABI too old")

    def test_out_of_range_payload_is_rejected(self):
        data = bytearray(self.original)
        payload_offset = struct.unpack("<Q", data[56:64])[0]
        data[56:64] = struct.pack("<Q", payload_offset + 1)
        fix_header_crc(data)
        self.db.write_bytes(data)
        self.assert_rejected("invalid manifest/payload ranges")

    def test_payload_corruption_is_rejected(self):
        data = bytearray(self.original)
        data[-1] ^= 0x01
        self.db.write_bytes(data)
        self.assert_rejected("payload CRC32 mismatch")

    def test_manifest_corruption_is_rejected(self):
        data = bytearray(self.original)
        data[128] = 0x00
        self.db.write_bytes(data)
        self.assert_rejected("manifest parse failed")

    def test_install_doc_has_no_generation_preservation_contract(self):
        text = DOC.read_text(encoding="utf-8")
        forbidden = [
            "/nearby/db/current",
            "previous generation remains active",
            "previous generation usable",
            "gen-00000042",
        ]
        for phrase in forbidden:
            self.assertNotIn(phrase, text)
        self.assertIn("no previous-generation preservation", text.lower())
        self.assertIn("no usable recognition DB", text)


if __name__ == "__main__":
    unittest.main()
