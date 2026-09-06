from pathlib import Path
import tempfile
import unittest

import zigbee_numbers_sources

MANUFACTURERS = '''/** source */
export enum ManufacturerCode {
    MATTER_STANDARD = 0x0000,
    PANASONIC = 0x0001,
    ACME = 0x1234,
    lower_case = 0x2222,
}
'''
CONSTS = '''
export const HA_PROFILE_ID = 0x0104;
export const SE_PROFILE_ID = 0x0109;
export const GP_PROFILE_ID = 0xa1e0;
export const TOUCHLINK_PROFILE_ID = 0xc05e;
export const WILDCARD_PROFILE_ID = 0xffff;
export const CUSTOM_VENDOR_PROFILE_ID = 0xc001;
'''


class ZigbeeNumbersSourcesTest(unittest.TestCase):
    def test_extracts_literal_manufacturer_codes_only(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "manufacturerCode.ts"
            path.write_text(MANUFACTURERS)
            records = zigbee_numbers_sources.extract_manufacturer_codes(path, "pin")
            self.assertEqual(
                [r["key"] for r in records],
                [
                    "zigbee:assigned:manufacturer:0000",
                    "zigbee:assigned:manufacturer:0001",
                    "zigbee:assigned:manufacturer:1234",
                ],
            )
            self.assertTrue(all(r["source_id"] == "zigbee_herdsman_defs" for r in records))
            self.assertTrue(all(r["classification"] == "DATA_ONLY" for r in records))

    def test_extracts_only_supported_public_profile_constants(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "consts.ts"
            path.write_text(CONSTS)
            records = zigbee_numbers_sources.extract_profile_ids(path, "pin")
            self.assertEqual(
                [r["key"] for r in records],
                [
                    "zigbee:assigned:profile:0104",
                    "zigbee:assigned:profile:0109",
                    "zigbee:assigned:profile:a1e0",
                    "zigbee:assigned:profile:c05e",
                ],
            )
            self.assertNotIn("zigbee:assigned:profile:ffff", {r["key"] for r in records})
            self.assertNotIn("zigbee:assigned:profile:c001", {r["key"] for r in records})

    def test_rejects_missing_expected_tables(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "empty.ts"
            path.write_text("export const X = 1;\n")
            with self.assertRaises(ValueError):
                zigbee_numbers_sources.extract_manufacturer_codes(path, "pin")
            with self.assertRaises(ValueError):
                zigbee_numbers_sources.extract_profile_ids(path, "pin")


if __name__ == "__main__":
    unittest.main()
