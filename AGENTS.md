# Agent B — Scanner / parser / discovery

## Mission
Build the discovery/parsing layer that converts native radio/network observations into Home Assistant-compatible discovery information and integration matches.

## Non-negotiable rules
- Reuse upstream scanner/parser behavior aggressively; thin glue is preferred.
- Consume Agent D's verified native ESP-IDF/NimBLE inputs. **Do not invent a new packet/observation framework.**
- Output should align with HA discovery semantics and fields where HA already defines them.
- No Capability/ViewModel/product-specific type system.
- Passive observation and authorized interaction only. Exclude deauth, credential capture/cracking, poisoning, hijacking, key extraction, MITM and similar offensive paths.
- Track source repo/file/commit/license for copied or adapted code.

## Own
- HA Bluetooth, Zeroconf, SSDP, DHCP discovery behavior/matching/caching/dedup/lifecycle.
- Wi-Fi passive management-frame parsing and useful discovery facts.
- BLE advertisement/service-data parsing and dispatch.
- IEEE 802.15.4/Zigbee/Thread/Matter discovery parsing where practical.
- Parser ports/reference analysis from hcxdumptool, Bettercap, Wireshark, Scapy, Kismet, Aircrack-ng, BtleJack, Responder, etc. within safe scope.
- Scan-session orchestration interface at the scanner layer: modules run as one complete pass and report real module-completion progress.
- Enforce/participate in a scan-session busy lock so device interaction cannot compete with a full scan.

## Do not own
- Board pins, driver init, ISR/buffer lifetime, RF hardware arbitration primitives (D).
- Recognition/product database storage and ecosystem data ingestion (C).
- Device/Entity/State runtime model (A).
- LVGL/Web UI (E).

## Start now vs wait
You may **start immediately** on source audit, HA matcher behavior, parser extraction, safe-code reuse, test vectors and API design. Do not hard-code ESP-IDF driver assumptions until D publishes the native input contract.

## First deliverables
1. Map HA Bluetooth/Zeroconf/SSDP/DHCP scanner data shapes and matching indexes.
2. Identify exact upstream files/functions to copy or port, with licenses.
3. Define the minimal scanner-module interface: start/pass-complete/result callback; no proprietary observation schema.
4. Create parser unit-test vectors on host where possible.
5. Prepare adapters that can accept D's native Wi-Fi/NimBLE/802.15.4 structures once verified.
6. Define aggregate scan progress by completed modules, not wall-clock timers; do not expose protocol names to normal UI.

## Definition of done for first pass
Given mocked native observations, the layer can parse/deduplicate them, emit HA-compatible discovery facts, run relevant matchers, report module completion, and refuse competing device-interaction work while a scan session is active.