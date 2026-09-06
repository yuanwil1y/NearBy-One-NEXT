# Third-party source ledger — Agent B

This branch records every upstream source consulted for scanner/parser behavior. Product code must not silently absorb incompatible/offensive upstream paths.

| Source | Immutable revision / ref | Path / subject | License | Classification | Use in B |
|---|---|---|---|---|---|
| Home Assistant Core | `2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48` | `homeassistant/generated/bluetooth.py`; Bluetooth discovery field semantics | Apache-2.0 | reference / translated semantics | HA-compatible matcher-input field names only; no generated matcher table copied into B |
| Agent D frozen branch in this repository | `accdf91441d0abaf6a5d69aac31394465ab49b06` | `native_handoff.h`, `radio_runtime.h`, `scan_session.h`, `docs/hardware/native-input-contract.md` | project-local | contract dependency | exact D->B native input/lifetime contract |
| Apache Mynewt NimBLE / ESP-NimBLE API | ESP-IDF v6.1-integrated API; D frozen contract above is authoritative for this project | `host/ble_gap.h`, `ble_gap_disc_desc`, `ble_gap_ext_disc_desc` | Apache-2.0 | reference-only | native descriptor field/status semantics; no upstream implementation copied |
| Bluetooth Core Assigned Numbers / GAP AD structure semantics | standard semantics | AD types 0x01/0x02..0x09/0x0A/0x16/0x19/0x20/0x21/0xFF | standards/reference | translated protocol semantics | bounded parser field interpretation |

## Explicit exclusions

Bettercap, Aircrack-ng, hcxdumptool, BtleJack, Responder, Wireshark, Scapy, Kismet, nRF Sniffer and mitmproxy remain research/reference sources unless a later commit adds an exact licensed source row above. No deauthentication, credential capture/cracking, poisoning, rogue authentication, BLE hijacking, Zigbee key extraction or TLS MITM code may be imported into product firmware.
