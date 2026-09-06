#!/usr/bin/env python3
"""Build a tiny B->C integration DB using Agent C's frozen generator code."""
from __future__ import annotations

from pathlib import Path
import sys
import tempfile

HA_BLE = '''from typing import Final
BLUETOOTH: Final[list[dict]] = [
    {"domain":"ble_alpha","manufacturer_id":123,"manufacturer_data_start":[1,2],"connectable":False},
    {"domain":"ble_same","manufacturer_id":321},
    {"domain":"ble_same","service_uuid":"00001111-0000-1000-8000-00805f9b34fb"},
    {"domain":"ble_other","service_data_uuid":"00002222-0000-1000-8000-00805f9b34fb"},
]
'''
HA_ZERO = '''HOMEKIT = {}
ZEROCONF = {
    "_demo._tcp.local.": [{"domain":"zc_demo","name":"demo-*","properties":{"model":"x*","id":"42"}}],
    "_amb._tcp.local.": [
        {"domain":"zc_amb_a","properties":{"model":"amb*"}},
        {"domain":"zc_amb_b","properties":{"model":"amb*"}},
    ],
}
'''
HA_SSDP = '''SSDP = {
    "ssdp_demo": [{"st":"urn:demo:1","manufacturer":"Acme"}],
    "ssdp_other": [{"manufacturer":"Acme","modelName":"Other"}],
}
'''
HA_DHCP = '''DHCP = [
    {"domain":"dhcp_demo","hostname":"demo-*","macaddress":"AABBCC*"},
    {"domain":"dhcp_amb_a","hostname":"amb-*","macaddress":"DDEEFF*"},
    {"domain":"dhcp_amb_b","hostname":"amb-*","macaddress":"DDEEFF*"},
]
'''


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} C_TOOLS_DIR OUT_DB", file=sys.stderr)
        return 2
    c_tools = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(c_tools))
    import full_dbgen  # type: ignore
    import nearby_dbgen  # type: ignore

    with tempfile.TemporaryDirectory() as td_text:
        td = Path(td_text)
        ha = td / "ha"
        ha.mkdir()
        (ha / "bluetooth.py").write_text(HA_BLE, encoding="utf-8")
        (ha / "zeroconf.py").write_text(HA_ZERO, encoding="utf-8")
        (ha / "ssdp.py").write_text(HA_SSDP, encoding="utf-8")
        (ha / "dhcp.py").write_text(HA_DHCP, encoding="utf-8")
        zha = td / "zha"
        z2m = td / "z2m"
        zha.mkdir()
        z2m.mkdir()
        full_dbgen.build_database(
            ha_bluetooth=ha / "bluetooth.py",
            ha_zeroconf=ha / "zeroconf.py",
            ha_ssdp=ha / "ssdp.py",
            ha_dhcp=ha / "dhcp.py",
            ha_rev="b-c-contract-pin",
            zha_root=zha,
            zha_rev="b-c-contract-pin",
            z2m_root=z2m,
            z2m_rev="b-c-contract-pin",
            out=out,
            db_version=2,
            build_epoch=1,
        )
        validation = nearby_dbgen.validate_file(out, release=True)
        if not validation["ok"]:
            print(validation["errors"], file=sys.stderr)
            return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
