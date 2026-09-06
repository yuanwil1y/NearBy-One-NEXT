from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import full_dbgen
import nearby_dbgen

HA_BLE = '''from typing import Final
BLUETOOTH: Final[list[dict]] = [
    {"domain":"ble_alpha","manufacturer_id":123,"manufacturer_data_start":[1,2],"connectable":False},
    {"domain":"ble_name","local_name":"Demo-*"},
    {"domain":"ble_same","manufacturer_id":321},
    {"domain":"ble_same","service_uuid":"00001111-0000-1000-8000-00805f9b34fb"},
    {"domain":"ble_other","service_data_uuid":"00002222-0000-1000-8000-00805f9b34fb"},
]
'''

HA_ZERO = '''HOMEKIT = {}
ZEROCONF = {
    "_demo._tcp.local.": [
        {"domain":"zc_demo","name":"demo-*","properties":{"model":"x*","id":"42"}},
        {"domain":"zc_other","properties":{"model":"other"}},
    ]
}
'''

HA_SSDP = '''SSDP = {
    "ssdp_demo": [{"st":"urn:demo:1","manufacturer":"Acme"}],
    "ssdp_other": [{"manufacturer":"Acme","modelName":"Other"}],
}
'''

HA_DHCP = '''DHCP = [
    {"domain":"dhcp_demo","hostname":"demo-*","macaddress":"AABBCC*"},
    {"domain":"dhcp_other","hostname":"other-*"},
]
'''


class RuntimeRecognitionContractTest(unittest.TestCase):
    def test_typed_ble_lan_contract_is_bounded_and_c_owned(self):
        cc = shutil.which("cc")
        if cc is None:
            self.skipTest("host C compiler unavailable")

        with tempfile.TemporaryDirectory() as td_text:
            td = Path(td_text)
            ha = td / "ha"
            ha.mkdir()
            (ha / "bluetooth.py").write_text(HA_BLE, encoding="utf-8")
            (ha / "zeroconf.py").write_text(HA_ZERO, encoding="utf-8")
            (ha / "ssdp.py").write_text(HA_SSDP, encoding="utf-8")
            (ha / "dhcp.py").write_text(HA_DHCP, encoding="utf-8")
            zha = td / "zha"
            zha.mkdir()
            z2m = td / "z2m"
            z2m.mkdir()
            out = td / "runtime-contract.nbdb"

            full_dbgen.build_database(
                ha_bluetooth=ha / "bluetooth.py",
                ha_zeroconf=ha / "zeroconf.py",
                ha_ssdp=ha / "ssdp.py",
                ha_dhcp=ha / "dhcp.py",
                ha_rev="ha-contract-pin",
                zha_root=zha,
                zha_rev="zha-contract-pin",
                z2m_root=z2m,
                z2m_rev="z2m-contract-pin",
                out=out,
                db_version=2,
                build_epoch=1,
            )
            validation = nearby_dbgen.validate_file(out, release=True)
            self.assertTrue(validation["ok"], validation["errors"])

            repo = Path(__file__).resolve().parents[2]
            exe = td / "host-runtime-contract"
            subprocess.run(
                [
                    cc,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{repo / 'components/recognition_db/include'}",
                    str(repo / "components/recognition_db/nearby_recognition_db.c"),
                    str(repo / "components/recognition_db/nearby_recognition_match.c"),
                    str(repo / "components/recognition_db/test/host_contract.c"),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            run = subprocess.run(
                [str(exe), str(out)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(run.stdout.strip(), "runtime recognition contract OK")

    def test_committed_ha_matcher_values_fit_recommended_workspace(self):
        repo = Path(__file__).resolve().parents[2]
        db_path = repo / "artifacts/pinned/nearby.nbdb"
        if not db_path.exists():
            self.skipTest("committed pinned DB not present")

        max_value = 0
        max_key = None
        with db_path.open("rb") as f:
            header = nearby_dbgen.read_header(f)
            base = header["payload_offset"]
            f.seek(base)
            _magic, count, entry_size, index_off, blob_off = nearby_dbgen.FLAT_HEADER.unpack(
                f.read(nearby_dbgen.FLAT_HEADER.size)
            )
            for i in range(count):
                f.seek(base + index_off + i * entry_size)
                key_off, key_len, _flags, _value_off, value_len = nearby_dbgen.FLAT_ENTRY.unpack(
                    f.read(entry_size)
                )
                f.seek(base + blob_off + key_off)
                key = f.read(key_len).decode("utf-8")
                queryable = (
                    (key.startswith("ble:") and not key.startswith("ble:assigned:"))
                    or key.startswith("lan:zeroconf:")
                    or key.startswith("lan:ssdp:")
                    or key.startswith("lan:dhcp:")
                )
                if queryable and value_len > max_value:
                    max_value = value_len
                    max_key = key

        self.assertLessEqual(
            max_value,
            4096,
            f"current HA matcher value exceeds NEARBY_DB_MATCH_WORKSPACE_RECOMMENDED: {max_key}={max_value}",
        )


if __name__ == "__main__":
    unittest.main()
