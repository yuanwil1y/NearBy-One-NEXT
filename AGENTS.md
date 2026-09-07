# NearBy One NEXT — Refactor Development Rules

## Canonical source of truth

On branch `refactor/native-kismet-baseline`, the root `README.md` is the canonical architecture and development baseline.

If older integration notes, frozen-agent documents, historical scanner architecture, PR descriptions, or legacy implementation details conflict with this branch's root README, the root README wins unless the user explicitly decides otherwise.

## Mission

Refactor NearBy One NEXT from a single integrated scanner firmware into an application-oriented ESP32-C6 handheld platform with three architectural levels:

```text
Application
├──────────────> Native APIs
├──────────────> kismet_*      acquisition / scanning / observation
├──────────────> wireshark_*   bounded protocol parsing / dissection
└──────────────> ha_*          recognition / Device-Entity-State semantics
```

The target board remains **Waveshare ESP32-C6-Touch-LCD-1.9**.

## Architecture rules

1. Native ESP-IDF, FreeRTOS, NimBLE, lwIP, IEEE 802.15.4, storage, and LVGL APIs remain directly available to applications.
2. Do not create wrappers whose main purpose is renaming a native API.
3. Keep BSP code limited to board-specific knowledge, hardware bring-up, shared-bus topology, and access to native handles.
4. Public Level-2 APIs are organized by upstream project identity only when a real reusable capability exists.
5. Do not create a namespace merely because a project was researched or used as a behavioral reference.
6. The first public firmware API families are `kismet_*`, `wireshark_*`, and `ha_*`.
7. Scapy remains host/test tooling unless a concrete firmware capability later justifies otherwise.
8. Bettercap, Aircrack-ng, hcxdumptool, BtleJack, nRF Sniffer, Responder, mitmproxy, and other researched projects remain provenance/reference sources until they gain distinct reusable runtime behavior.
9. Existing `nearby_*` public names are migration input, not the target umbrella API design.
10. Multiple project APIs may share internal implementations. Do not duplicate parser/scanner code just to preserve namespace identity.
11. Applications may combine native APIs and any Level-2 project APIs freely.
12. Do not create replacement UI, RTOS, radio, storage, packet, or networking frameworks parallel to LVGL/FreeRTOS/ESP-IDF without a concrete reusable requirement approved by the user.
13. Existing scanner code is migration material. Preserve useful behavior, not obsolete component boundaries.
14. Keep the tree buildable during staged migration.

## Public API roles

Use these defaults when deciding ownership:

```text
kismet_*       acquire observations
wireshark_*    parse/dissect protocol data
ha_*           recognize/model device semantics
native APIs    direct low-level control
application    product workflow/UI/policy
```

Do not force an API into a project namespace when the implementation does not actually provide that project's distinct reusable capability.

## Kismet boundary

Kismet owns finite acquisition workflows such as Wi-Fi active/passive scans, BLE scans, IEEE 802.15.4 scans, LAN discovery scans, bounded deduplication, observation statistics, and useful scan mechanics.

It does not own trivial aliases for native lifecycle functions.

Good:

```c
kismet_wifi_scan_passive(...);
kismet_ble_scan(...);
kismet_i154_scan(...);
```

Bad:

```c
kismet_wifi_stop(); /* if it only forwards to esp_wifi_stop() */
```

Whole-product scan ordering, enabled modules, progress UI, touch locking, and fixed “scan all” policy belong to the application unless they become independently reusable across Apps.

## Wireshark boundary

Wireshark owns bounded dissector-style parsing helpers migrated from the existing parser code, including where applicable:

```c
wireshark_80211_parse_mgmt(...);
wireshark_ble_parse_adv(...);
wireshark_mdns_parse(...);
wireshark_ssdp_parse(...);
wireshark_dhcp_parse(...);
wireshark_i154_parse_mac(...);
wireshark_zigbee_parse(...);
```

Preserve fixed storage limits, malformed/partial handling, and native metadata needed by applications.

Do not claim a protocol parser exists under a Wireshark name when the firmware currently implements only a higher-level discovery semantic subset.

## Home Assistant boundary

Home Assistant owns typed device recognition and the RAM-bounded Device / Entity / State semantic API.

Target recognition examples:

```c
ha_match_ble(...);
ha_match_zeroconf(...);
ha_match_ssdp(...);
ha_match_dhcp(...);
ha_match_zigbee(...);
ha_match_matter(...);
```

Existing `ha_device_*`, `ha_entity_*`, `ha_state_*`, and service APIs should be retained/stabilized where useful.

HA semantics are optional per App. A Wi-Fi Analyzer or Packet Inspector must not be forced through HA Device/Entity/State.

## Resource ownership

The ESP32-C6 radio, Wi-Fi state, NimBLE state, IEEE 802.15.4 state, shared LCD/SD SPI bus, storage, and LVGL owner task are shared resources.

High-level acquisition workflows should generally follow:

```text
inspect current state
-> acquire minimum required lease/lock
-> perform workflow
-> restore previous state when practical
-> release lease/lock
```

A small internal resource coordinator for leases/mutexes/cancellation is allowed. It must not become another HAL or mirror native driver APIs.

## Migration direction

Current code should move roughly as follows:

```text
nearby_board          -> narrow BSP
nearby_lvgl_port      -> thin lvgl_port
radio_runtime         -> remove rename-wrapper role; direct native lifecycle + internal resource support
native_handoff        -> retain useful bounded callback handoff internally
scan_coordinator      -> split reusable mechanics from App fixed scan policy
scan_session          -> internal resource/Kismet acquisition support
discovery_parser      -> wireshark_* parsers + kismet_* acquisition + ha_* semantic adapters
recognition_db        -> bounded recognition implementation behind ha_* matching entry points where appropriate
ha_core               -> stable ha_* semantic APIs
nearby_scan_pipeline  -> mostly App composition; migrate reusable mechanics downward
nearby_semantic_apply -> App glue
main.c                -> boot + platform/app runtime startup only
```

## BSP boundary

The BSP may own board-specific facts such as:

- Waveshare pin definitions;
- ST7789V2 display bring-up;
- CST816 touch bring-up;
- shared SPI2 initialization for LCD + SD;
- I2C board bus initialization;
- backlight/power/reset sequencing;
- native ESP-IDF handle exposure.

The BSP must not own scanning, protocol parsing, recognition, UI pages, or application workflows.

## LVGL rule

LVGL is exposed directly to applications.

A thin port may initialize display/touch integration and enforce the LVGL owner-task rule, but do not build wrapper APIs that mirror ordinary `lv_*` widgets/functions.

## FreeRTOS rule

Applications and Level-2 modules may directly use FreeRTOS tasks, queues, mutexes, event groups, and timers.

Do not create a second project-specific RTOS abstraction solely for naming consistency.

## Provenance and licensing

When behavior is materially copied, translated, adapted, generated, or derived from an upstream project, preserve the relevant repository, immutable revision/tag, source path, license, and provenance classification.

A public namespace is not a substitute for legal/source provenance documentation.

Reference-only sources must remain reference-only unless project distribution policy explicitly permits incorporation.

## Build baseline

Target:

- Framework: ESP-IDF
- MCU target: `esp32c6`
- Board: Waveshare ESP32-C6-Touch-LCD-1.9
- Display: 170x320 portrait, ST7789V2 baseline
- Touch: CST816
- No PSRAM assumed

Keep normal ESP-IDF build/flash behavior:

```text
cd firmware
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```

Do not invent a custom firmware packaging path unless the hardware or release process actually requires it.

## Security boundary

Passive discovery, protocol parsing, diagnostics, packet observation/capture where appropriate, and authorized device interaction are within scope.

Do not add credential theft, unauthorized access, destructive RF interference, persistence on third-party devices, or access-control bypass features.

Researching or referencing a security project does not imply that all of its offensive capabilities belong in NearBy One NEXT.

## Working principle

When deciding where code belongs, use this rule:

> Native APIs provide primitives. Kismet acquires. Wireshark dissects. Home Assistant understands. Apps compose product experiences.

That is the architecture to preserve throughout this refactor.
