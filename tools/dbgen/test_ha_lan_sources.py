from pathlib import Path
import tempfile
import unittest

import ha_lan_sources

ZEROCONF = '''HOMEKIT = {"Demo": {"domain": "demo", "always_discover": True}}
ZEROCONF = {
 "_demo._tcp.local.": [
   {"domain": "demo"},
   {"domain": "demo2", "name": "unit-*", "properties": {"model": "x*"}},
 ]
}
'''
SSDP = '''SSDP = {
 "demo": [{"st": "urn:demo:1", "manufacturer": "Acme"}],
 "demo2": [{"manufacturer": "Other"}],
}
'''
DHCP = '''from typing import Final
DHCP: Final[list[dict[str, str | bool]]] = [
 {"domain": "demo", "hostname": "demo-*"},
 {"domain": "demo2", "hostname": "hub", "macaddress": "aabbcc*"},
 {"domain": "demo3", "registered_devices": True},
]
'''


class HaLanSourcesTest(unittest.TestCase):
    def test_extracts_generated_zeroconf_matchers_without_homekit_runtime(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "zeroconf.py"
            p.write_text(ZEROCONF)
            records = ha_lan_sources.extract_ha_zeroconf(p, "ha-pin")
        self.assertEqual(len(records), 2)
        self.assertTrue(all(r["source_revision"] == "ha-pin" for r in records))
        self.assertTrue(all(r["key"].startswith("lan:zeroconf:_demo._tcp.local.\x1f") for r in records))
        self.assertEqual(records[1]["matcher"]["properties"]["model"], "x*")

    def test_extracts_ssdp_with_st_then_manufacturer_anchor(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "ssdp.py"
            p.write_text(SSDP)
            records = ha_lan_sources.extract_ha_ssdp(p, "ha-pin")
        self.assertEqual(len(records), 2)
        by_domain = {r["domain"]: r for r in records}
        self.assertEqual(by_domain["demo"]["anchor_field"], "st")
        self.assertEqual(by_domain["demo2"]["anchor_field"], "manufacturer")
        self.assertTrue(by_domain["demo"]["key"].startswith("lan:ssdp:st:urn:demo:1"))

    def test_extracts_dhcp_with_mac_hostname_and_registered_anchors(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "dhcp.py"
            p.write_text(DHCP)
            records = ha_lan_sources.extract_ha_dhcp(p, "ha-pin")
        self.assertEqual(len(records), 3)
        by_domain = {r["domain"]: r for r in records}
        self.assertEqual(by_domain["demo"]["anchor_field"], "hostname")
        self.assertEqual(by_domain["demo2"]["anchor_value"], "AABBCC*")
        self.assertEqual(by_domain["demo3"]["anchor_field"], "registered")

    def test_rejects_dynamic_python_in_generated_assignment(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.py"
            p.write_text('DHCP = make_matchers()\n')
            with self.assertRaisesRegex(ValueError, "not a pure generated literal"):
                ha_lan_sources.extract_ha_dhcp(p, "ha-pin")


if __name__ == "__main__":
    unittest.main()
