from pathlib import Path
import tempfile
import unittest

import matter_sources

FIXTURE = '''matterManufacturer:
  # Aqara
  - id: "4447/10242"
    deviceLabel: Aqara Smart Lock U200
    vendorId: 0x115F
    productId: 0x2802
    deviceProfileName: lock-user-pin
  - id: plain-id
    deviceLabel: Demo
    vendorId: 4660
    productId: 7 # decimal PID
    deviceProfileName: demo
  - id: incomplete
    vendorId: 0x1234
matterGeneric:
  - id: generic-lock
    deviceLabel: Generic Lock
    deviceTypes:
      - id: 0x000A
    deviceProfileName: generic
zigbeeManufacturer:
  - id: unrelated
    vendorId: 0x9999
    productId: 0x9999
'''


class MatterSourcesTest(unittest.TestCase):
    def test_extracts_only_literal_matter_manufacturer_vid_pid(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "fingerprints.yml"
            path.write_text(FIXTURE)
            records = matter_sources.extract_file(path, "pin", "drivers/matter/fingerprints.yml")
            self.assertEqual([r["key"] for r in records], ["matter:vidpid:115f:2802", "matter:vidpid:1234:0007"])
            first = records[0]
            self.assertEqual(first["fingerprint_id"], "4447/10242")
            self.assertEqual(first["device_label"], "Aqara Smart Lock U200")
            self.assertEqual(first["device_profile_name"], "lock-user-pin")
            self.assertEqual(first["source_id"], "smartthings_matter_fingerprints")
            self.assertEqual(first["classification"], "DATA_ONLY")
            self.assertIn("not_authoritative", first["catalog_semantics"])

    def test_tree_reports_files_and_records(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            one = root / "a" / "fingerprints.yml"
            one.parent.mkdir()
            one.write_text(FIXTURE)
            two = root / "b" / "fingerprints.yml"
            two.parent.mkdir()
            two.write_text("zigbeeManufacturer:\n  - id: x\n")
            records, stats = matter_sources.extract_tree(root, "pin")
            self.assertEqual(len(records), 2)
            self.assertEqual(stats["fingerprint_files_scanned"], 2)
            self.assertEqual(stats["fingerprint_files_with_matter_manufacturer"], 1)
            self.assertEqual(stats["matter_vid_pid_records"], 2)


if __name__ == "__main__":
    unittest.main()
