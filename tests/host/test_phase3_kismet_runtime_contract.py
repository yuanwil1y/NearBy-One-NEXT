from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KISMET = ROOT / "firmware" / "components" / "kismet"
LEGACY = ROOT / "firmware" / "components" / "discovery_parser"

EXPECTED = {
    "wifi_scan_runtime.c": ["kismet_wifi_scan_active", "kismet_wifi_scan_passive", "wireshark_80211_parse_mgmt", "nearby_wifi_rx_drop_count"],
    "ble_scan_runtime.c": ["kismet_ble_scan", "wireshark_ble_parse_adv", "nearby_ble_rx_drop_count"],
    "i154_scan_runtime.c": ["kismet_i154_scan", "wireshark_i154_parse_mac", "wireshark_zigbee_parse", "nearby_i154_rx_drop_count"],
    "lan_scan_runtime.c": ["kismet_mdns_discover", "kismet_ssdp_discover", "kismet_dhcp_observe", "wireshark_mdns_parse", "wireshark_ssdp_parse", "wireshark_dhcp_parse"],
}
FORBIDDEN = ["nearby_wifi_parse_management_frame", "nearby_ble_parse_advertisement", "nearby_i154_parse_mac", "nearby_zigbee_parse_from_mac", "nearby_mdns_parse", "nearby_ssdp_parse", "nearby_dhcp_parse"]
for name, symbols in EXPECTED.items():
    text = (KISMET / name).read_text()
    for symbol in symbols:
        assert symbol in text, f"{name} missing {symbol}"
    for symbol in FORBIDDEN:
        assert symbol not in text, f"{name} still calls legacy parser {symbol}"
    assert "kismet_scan_all" not in text

dedup = (KISMET / "transport_dedup.c").read_text()
assert "KISMET_DEDUP_SLOT_COUNT" in dedup
assert "kismet_dedup_init" in dedup and "kismet_dedup_reset" in dedup

WRAPPERS = {
    "wifi_scan_runtime.c": ["kismet_wifi_scan_active", "kismet_wifi_scan_passive"],
    "ble_scan_runtime.c": ["kismet_ble_scan"],
    "i154_scan_runtime.c": ["kismet_i154_scan"],
    "lan_scan_runtime.c": ["kismet_lan_scan_prerequisite_ready", "kismet_mdns_discover", "kismet_ssdp_discover", "kismet_dhcp_observe"],
    "transport_dedup.c": ["kismet_dedup_reset", "kismet_ble_dedup_should_emit"],
}
for name, symbols in WRAPPERS.items():
    text = (LEGACY / name).read_text()
    for symbol in symbols:
        assert symbol in text
    assert "native_handoff.h" not in text
    assert "radio_runtime.h" not in text

coordinator = (ROOT / "firmware/components/scan_coordinator/scan_default_runners.c").read_text()
for symbol in ("kismet_wifi_scan_active", "kismet_wifi_scan_passive", "kismet_mdns_discover", "kismet_ssdp_discover", "kismet_dhcp_observe", "kismet_ble_scan", "kismet_i154_scan"):
    assert symbol in coordinator

assert "kismet_scan_all" not in "\n".join(p.read_text() for p in KISMET.rglob("*") if p.is_file())
print("Phase 3 Kismet runtime ownership contract: OK")
