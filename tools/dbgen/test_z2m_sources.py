from pathlib import Path
import tempfile
import unittest

import z2m_sources

SOURCE = r'''
import * as fz from "../converters/fromZigbee";
export const definitions: DefinitionWithExtend[] = [
    {
        fingerprint: [{modelID: "SmokeSensor-EM", manufacturerName: "Trust"}],
        zigbeeModel: ["ZSDR-850"],
        model: "ZSDR-850",
        vendor: "Trust",
        description: "Smoke detector",
        fromZigbee: [fz.ias_smoke_alarm_1],
    },
    {
        zigbeeModel: ["losfena"],
        fingerprint: tuya.fingerprint("TS0601", ["_TZE200_wlosfena", "_TZE204_demo"]),
        model: "07703L",
        vendor: "Immax",
        description: "Smart valve",
        configure: async (device) => dangerousRuntimeCall(device),
    },
    {
        fingerprint: someRuntimeHelper(),
        model: "DYNAMIC",
        vendor: "SkipCo",
    },
];
'''


class Z2mSourcesTest(unittest.TestCase):
    def test_extracts_only_static_identity_without_executing_converters(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "demo.ts").write_text(SOURCE)
            records, stats = z2m_sources.extract_z2m(root, "z2m-pin")
        keys = {r["key"] for r in records}
        self.assertIn("zigbee:model:ZSDR-850", keys)
        self.assertIn("zigbee:model:losfena", keys)
        self.assertIn("zigbee:fingerprint:Trust\x1fSmokeSensor-EM", keys)
        self.assertIn("zigbee:fingerprint:_TZE200_wlosfena\x1fTS0601", keys)
        self.assertIn("zigbee:fingerprint:_TZE204_demo\x1fTS0601", keys)
        self.assertTrue(all(r["source_revision"] == "z2m-pin" for r in records))
        self.assertTrue(all(r["source_classification"] == "DATA+PARSER" for r in records))
        self.assertTrue(all("fromZigbee" not in r and "configure" not in r for r in records))
        self.assertEqual(stats["definition_objects"], 3)
        self.assertEqual(stats["definitions_with_static_identity"], 2)
        self.assertEqual(stats["dynamic_fingerprint_fields_unresolved"], 1)

    def test_literal_metadata_is_preserved(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "demo.ts").write_text(SOURCE)
            records, _ = z2m_sources.extract_z2m(root, "z2m-pin")
        record = next(r for r in records if r["key"] == "zigbee:model:ZSDR-850")
        self.assertEqual(record["product_model"], "ZSDR-850")
        self.assertEqual(record["vendor"], "Trust")
        self.assertEqual(record["description"], "Smoke detector")
        self.assertEqual(record["classification"], "DATA_ONLY")

    def test_template_literal_braces_do_not_break_definition_scanner(self):
        text = '''export const definitions = [{zigbeeModel: ["X"], model: "P", vendor: "V", description: `value ${x}` }];'''
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "demo.ts").write_text(text)
            records, stats = z2m_sources.extract_z2m(root, "pin")
        self.assertEqual(stats["definition_objects"], 1)
        self.assertEqual([r["key"] for r in records], ["zigbee:model:X"])


if __name__ == "__main__":
    unittest.main()
