# API ownership after Phase 3

Phase 2 moved bounded protocol parsers to `wireshark` and HA semantics to
`home_assistant`. Phase 3 moves finite acquisition and scan-generation dedup to
`kismet` without changing product orchestration or radio lifecycle ownership.

| Capability | Public owner | Current implementation |
|---|---|---|
| Wi-Fi/BLE/LAN/802.15.4/Zigbee parsing | `wireshark_*` | `wireshark` component |
| Wi-Fi active/passive finite acquisition | `kismet_wifi.h` | `kismet` component |
| BLE finite acquisition | `kismet_ble.h` | `kismet` component |
| IEEE 802.15.4 finite acquisition | `kismet_i154.h` | `kismet` component |
| mDNS/SSDP/DHCP finite LAN acquisition | `kismet_lan.h` | `kismet` component |
| Bounded generation-scoped dedup | `kismet_dedup.h` | `kismet` component |
| Matter discovery / typed recognition semantics | `ha_*` | `home_assistant` component |
| Radio lifecycle and bounded callback handoff | internal legacy runtime support | intentionally retained for Phase 4 |
| Scanner order/progress/touch/UI policy | application/coordinator | intentionally unchanged |

Legacy `nearby_*` scan and dedup types remain aliases to Kismet types, and the
legacy link symbols are thin forwarders to the Kismet implementations. Existing
callers continue to build while new callers can use the target APIs directly.

`native_handoff` remains internal support for bounded callback handoff. Kismet
calls the Phase 2 `wireshark_*` parsers directly; it does not duplicate parser
logic. No `kismet_scan_all()` exists and fixed product ordering stays in
`scan_coordinator`.
