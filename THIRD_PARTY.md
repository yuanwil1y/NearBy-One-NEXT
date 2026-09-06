# Third-party source ledger — Agent B

This branch records every upstream source consulted for scanner/parser behavior. Product code must not silently absorb incompatible/offensive upstream paths.

| Source | Immutable revision / ref | Path / subject | License | Classification | Use in B |
|---|---|---|---|---|---|
| Home Assistant Core | `2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48` | `homeassistant/generated/bluetooth.py`; `components/bluetooth/match.py` | Apache-2.0 | reference / translated semantics | HA-compatible Bluetooth matcher-input field names only; no generated matcher table copied into B |
| Home Assistant Core | `2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48` | `homeassistant/generated/zeroconf.py`, `generated/ssdp.py`, `generated/dhcp.py`, `components/zeroconf/discovery.py`, `components/dhcp/__init__.py` | Apache-2.0 | reference / translated semantics | LAN discovery fact casing/field semantics for C; no C-owned domain/digest matcher records copied into B |
| Agent C current branch in this repository | `bae5150aa7d5c512aa072cb1198d65758b58d6f3` | `tools/dbgen/nearby_dbgen.py`, `z2m_sources.py`, `matter_sources.py`, `ha_lan_sources.py`; `nearby_recognition_db.h` | project-local + recorded upstream provenance | contract dependency / test fixture | exact observation-derived key formatting and `AMBIGUOUS` result contract only; B performs no recognition |
| Agent D frozen branch in this repository | `accdf91441d0abaf6a5d69aac31394465ab49b06` | `native_handoff.h`, `radio_runtime.h`, `scan_session.h`, `docs/hardware/native-input-contract.md` | project-local | contract dependency | exact D->B native input/lifetime contract |
| ESP-IDF | `v6.1` | Wi-Fi promiscuous/active-scan types and IEEE 802.15.4 public receive/frame-info APIs | Apache-2.0 | reference / native API use | native metadata and lifecycle semantics; no replacement D driver copied into B |
| Apache Mynewt NimBLE / ESP-NimBLE API | ESP-IDF v6.1-integrated API; D frozen contract above is authoritative for this project | `host/ble_gap.h`, `ble_gap_disc_desc`, `ble_gap_ext_disc_desc` | Apache-2.0 | reference-only | native descriptor field/status semantics; no upstream implementation copied |
| Bluetooth Core Assigned Numbers / GAP AD structure semantics | standard semantics | AD types 0x01/0x02..0x09/0x0A/0x16/0x19/0x20/0x21/0xFF | standards/reference | translated protocol semantics | bounded parser field interpretation |
| Project CHIP / connectedhomeip | `e054bb8ac69b87d8cd11d720f6ade008e0b928c9` | `src/ble/CHIPBleServiceData.h`, `src/platform/ESP32/nimble/ChipDeviceScanner.cpp`; commissioning mDNS test semantics | Apache-2.0 | reference / translated semantics | Matter FFF6 identification layout, discriminator/VID/PID and `_matterc._udp.local.` TXT normalization; no CHIP runtime copied |
| IEEE 802.15.4 / Zigbee public protocol semantics | standards/reference | 802.15.4 MAC addressing/security header; Zigbee NWK/APS/ZDO and standard Basic cluster fields | standards/reference | translated protocol semantics | bounded passive parsing of standard discovery fields only; secured payloads are not decrypted |

## Explicit exclusions

Bettercap, Aircrack-ng, hcxdumptool, BtleJack, Responder, Wireshark, Scapy, Kismet, nRF Sniffer and mitmproxy remain research/reference sources unless a later commit adds an exact licensed source row above. No deauthentication, credential capture/cracking, poisoning, rogue authentication, BLE hijacking, Zigbee key extraction or TLS MITM code may be imported into product firmware.
