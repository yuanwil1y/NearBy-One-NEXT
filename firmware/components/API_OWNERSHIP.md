# Phase 1 public API ownership

This file records the ownership contract established by Phase 1. It does not
move parser, scanner, radio-lifecycle, or Scanner App implementation.

| Capability | Public owner | Phase 1 implementation status |
|---|---|---|
| Wi-Fi management facts/parser contract | `wireshark_80211.h` | implementation remains in `discovery_parser` |
| BLE advertisement facts/parser contract | `wireshark_ble.h` | implementation remains in `discovery_parser` |
| mDNS/SSDP/DHCP parser contracts | `wireshark_mdns.h`, `wireshark_ssdp.h`, `wireshark_dhcp.h` | implementation remains in `discovery_parser` |
| IEEE 802.15.4 / Zigbee parser contracts | `wireshark_i154.h`, `wireshark_zigbee.h` | implementation remains in `discovery_parser` |
| Wi-Fi/BLE/802.15.4 finite scan contracts | `kismet_*.h` | implementation remains in `discovery_parser` |
| LAN acquisition contracts: `kismet_mdns_discover()`, `kismet_ssdp_discover()`, `kismet_dhcp_observe()` | `kismet_lan.h` | implementation remains in `discovery_parser` |
| Generation-scoped scan dedup state | `kismet_dedup.h` | implementation remains in `discovery_parser` |
| Matter discovery semantics | `ha_discovery.h` | implementation remains in `discovery_parser` |
| Typed BLE/Zeroconf/SSDP/DHCP recognition | `ha_recognition.h` | adapter implementation remains in `discovery_parser`; `.nbdb` runtime remains in `recognition_db` |
| Device / Entity / State runtime | existing `components/ha_core/include/ha_core.h` | retained as-is |
| Radio lifecycle and bounded callback handoff | native/legacy internal components | intentionally not moved in Phase 1 |
| Scanner ordering, progress, UI, touch policy | application layer | intentionally unchanged in Phase 1 |

## Compatibility rule

Legacy `nearby_*` headers remain supported. Their public data types are now
compatibility aliases to the new owner types where Phase 1 assigns ownership.
Legacy function symbols remain the only linked implementations in this phase;
new project-namespaced function declarations are contracts for later mechanical
migration phases.

## Dependency direction

```text
wireshark       -> bounded C types only
kismet          -> wireshark + ESP-IDF/FreeRTOS public types
home_assistant  -> wireshark + recognition_db + existing ha_core
discovery_parser (legacy implementation)
                -> kismet + wireshark + home_assistant + existing runtime deps
```

No new component depends on `discovery_parser`, so the new API layer does not
pull legacy radio/scan runtime ownership upward.
