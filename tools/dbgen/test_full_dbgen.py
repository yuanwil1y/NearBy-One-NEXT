from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest

import full_dbgen
import nearby_dbgen

HA_BLE = '''from typing import Final
BLUETOOTH: Final[list[dict]] = [{"domain":"demo","manufacturer_id":123}]
'''
HA_ZERO = '''HOMEKIT = {"Demo*": {"domain": "demo", "always_discover": True}}
ZEROCONF = {"_demo._tcp.local.": [{"domain": "demo", "properties": {"model": "x*"}}]}
'''
HA_SSDP = '''SSDP = {"demo": [{"st": "urn:demo:1", "manufacturer": "Acme"}]}
'''
HA_DHCP = '''DHCP = [{"domain": "demo", "hostname": "demo-*", "macaddress": "AABBCC*"}]
'''
ZHA = '''MODELS_INFO="models_info"
class Demo:
 signature={MODELS_INFO:[("Acme","ZB-1")]}
'''
Z2M = '''export const definitions: DefinitionWithExtend[] = [
 {zigbeeModel: ["ZB-2"], fingerprint: [{modelID: "ZB-2", manufacturerName: "Other"}], model: "P2", vendor: "Other"},
];
'''


class FullDbgenTest(unittest.TestCase):
    def test_combined_sources_build_validate_and_lookup(self):
        with tempfile.TemporaryDirectory() as td_text:
            td = Path(td_text)
            ha = td / "ha"
            ha.mkdir()
            (ha / "bluetooth.py").write_text(HA_BLE)
            (ha / "zeroconf.py").write_text(HA_ZERO)
            (ha / "ssdp.py").write_text(HA_SSDP)
            (ha / "dhcp.py").write_text(HA_DHCP)
            zha = td / "zha"
            zha.mkdir()
            (zha / "demo.py").write_text(ZHA)
            z2m = td / "z2m"
            z2m.mkdir()
            (z2m / "demo.ts").write_text(Z2M)
            out = td / "combined.nbdb"

            summary = full_dbgen.build_database(
                ha_bluetooth=ha / "bluetooth.py",
                ha_zeroconf=ha / "zeroconf.py",
                ha_ssdp=ha / "ssdp.py",
                ha_dhcp=ha / "dhcp.py",
                ha_rev="ha-pin",
                zha_root=zha,
                zha_rev="zha-pin",
                z2m_root=z2m,
                z2m_rev="z2m-pin",
                out=out,
                db_version=2,
                build_epoch=1,
            )
            validation = nearby_dbgen.validate_file(out, release=True)
            self.assertTrue(validation["ok"], validation["errors"])
            self.assertEqual(len(validation["manifest"]["sources"]), 3)
            self.assertEqual(summary["raw_records"], 8)
            expected_types = {
                "ha_bluetooth_matcher",
                "ha_homekit_model_matcher",
                "ha_zeroconf_matcher",
                "ha_ssdp_matcher",
                "ha_dhcp_matcher",
                "zha_manufacturer_model",
                "z2m_zigbee_model",
                "z2m_fingerprint",
            }
            self.assertEqual(set(summary["counts"]), expected_types)
            self.assertIsNotNone(nearby_dbgen.lookup(out, "lan:homekit:model:Demo*\x1fdemo"))
            for key in (
                "lan:zeroconf:_demo._tcp.local.\x1fdemo\x1f",
                "lan:ssdp:st:urn:demo:1\x1fdemo\x1f",
                "lan:dhcp:mac:AABBCC*\x1fdemo\x1f",
            ):
                self.assertTrue(any(k.startswith(key) for k, _flags in _entries(out)))
            self.assertIsNotNone(nearby_dbgen.lookup(out, "zigbee:model:ZB-2"))
            self.assertIsNotNone(nearby_dbgen.lookup(out, "zigbee:fingerprint:Other\x1fZB-2"))

    def test_unresolved_collision_returns_ambiguity_not_lexical_winner(self):
        records = [
            {"key": "zigbee:model:X", "record_type": "demo", "source_id": "z2m_converters", "claim": "B"},
            {"key": "zigbee:model:X", "record_type": "demo", "source_id": "zha_quirks", "claim": "A"},
            {"key": "zigbee:model:Y", "record_type": "demo", "source_id": "ha_core", "claim": "same"},
            {"key": "zigbee:model:Y", "record_type": "demo", "source_id": "ha_core", "claim": "same"},
        ]
        runtime_records, ledger = full_dbgen.dedupe_with_ledger(records)
        self.assertEqual(len(runtime_records), 2)
        self.assertEqual(len(ledger), 1)
        collision = ledger[0]
        self.assertEqual(collision["key"], "zigbee:model:X")
        self.assertEqual(collision["candidate_count"], 2)
        self.assertEqual(collision["sources"], ["z2m_converters", "zha_quirks"])
        self.assertEqual(collision["resolution"], "unresolved_ambiguous")
        self.assertEqual(collision["runtime_result"], "ambiguous")
        self.assertNotIn("winner", collision)
        self.assertNotIn("winner_reason", collision)
        self.assertTrue(collision["manual_review"])

        ambiguous = next(r for r in runtime_records if r["key"] == "zigbee:model:X")
        self.assertEqual(ambiguous["record_type"], "recognition_ambiguity")
        self.assertEqual(ambiguous["match_status"], "ambiguous")
        self.assertEqual(ambiguous["candidate_count"], 2)
        self.assertEqual({c["claim"] for c in ambiguous["candidates"]}, {"A", "B"})
        self.assertNotIn("claim", ambiguous)
        self.assertNotIn("winner", ambiguous)

    def test_ambiguity_index_flag_is_fail_closed_for_c_reader(self):
        records, _ledger = full_dbgen.dedupe_with_ledger([
            {"key": "zigbee:model:X", "record_type": "demo", "source_id": "s1", "claim": "A"},
            {"key": "zigbee:model:X", "record_type": "demo", "source_id": "s2", "claim": "B"},
        ])
        with tempfile.TemporaryDirectory() as td_text:
            td = Path(td_text)
            out = td / "ambiguous.nbdb"
            payload = full_dbgen.build_flat_payload(records)
            manifest = nearby_dbgen.canonical_json({
                "container": "nearby-recognition-db",
                "format": "flat-v0",
                "schema_version": nearby_dbgen.SCHEMA_VERSION,
                "db_version": 1,
                "reader_abi": nearby_dbgen.READER_ABI,
                "record_count": 1,
                "sources": [],
            })
            header = nearby_dbgen.make_header(
                db_version=1,
                source_count=0,
                file_size=nearby_dbgen.HEADER_SIZE + len(manifest) + len(payload),
                manifest_size=len(manifest),
                payload=payload,
                build_epoch=1,
            )
            out.write_bytes(header + manifest + payload)
            looked_up = nearby_dbgen.lookup(out, "zigbee:model:X")
            self.assertEqual(looked_up["record_type"], "recognition_ambiguity")
            self.assertEqual(looked_up["match_status"], "ambiguous")
            entries = list(_entries(out))
            self.assertEqual(entries, [("zigbee:model:X", full_dbgen.AMBIGUOUS_INDEX_FLAG)])

            cc = shutil.which("cc")
            if cc is None:
                self.skipTest("host C compiler unavailable")
            repo = Path(__file__).resolve().parents[2]
            exe = td / "host-smoke"
            subprocess.run([
                cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{repo / 'components/recognition_db/include'}",
                str(repo / "components/recognition_db/nearby_recognition_db.c"),
                str(repo / "components/recognition_db/test/host_smoke.c"),
                "-o", str(exe),
            ], check=True)
            run = subprocess.run([str(exe), str(out), "zigbee:model:X"], check=True, capture_output=True, text=True)
            self.assertTrue(run.stdout.startswith("AMBIGUOUS\n"), run.stdout)
            value = json.loads(run.stdout.split("\n", 1)[1])
            self.assertEqual(value["match_status"], "ambiguous")


def _entries(path: Path):
    with path.open("rb") as f:
        h = nearby_dbgen.read_header(f)
        base = h["payload_offset"]
        f.seek(base)
        _magic, count, entry_size, index_off, blob_off = nearby_dbgen.FLAT_HEADER.unpack(f.read(nearby_dbgen.FLAT_HEADER.size))
        for i in range(count):
            f.seek(base + index_off + i * entry_size)
            key_off, key_len, flags, _value_off, _value_len = nearby_dbgen.FLAT_ENTRY.unpack(f.read(entry_size))
            f.seek(base + blob_off + key_off)
            yield f.read(key_len).decode("utf-8"), flags


if __name__ == "__main__":
    unittest.main()
