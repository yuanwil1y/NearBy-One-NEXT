# NearBy One NEXT

NearBy One NEXT is a portable ESP32-C6 platform for discovering, inspecting, understanding, and interacting with nearby devices.

Target board: **Waveshare ESP32-C6-Touch-LCD-1.9**  
Framework: **ESP-IDF + FreeRTOS + LVGL**  
Architecture baseline: **Native APIs + project-organized Level-2 APIs + Applications**

> This branch is the architecture reset baseline of NearBy One NEXT.
>
> The firmware is no longer organized as one scanner application with private wrappers. It is being rebuilt as a handheld platform where applications may use ESP-IDF / FreeRTOS / LVGL directly, call reusable Level-2 APIs organized by the mature upstream projects that inspired them, or freely combine both approaches.

---

## 1. Product direction

NearBy One NEXT is intended to become a small, open, application-oriented handheld platform built around the ESP32-C6 radio stack, touch display, removable storage, protocol analysis, device discovery, device recognition, and a growing collection of reusable wireless/network tools.

The long-term product idea is closer to a **programmable wireless Swiss-army knife** than a single-purpose scanner.

The firmware has three architectural levels:

```text
Application
   │
   ├──────────────────────────────> Native APIs
   │                                 ESP-IDF / FreeRTOS / LVGL / NimBLE / lwIP
   │
   └──────────────────────────────> Level-2 Project APIs
                                     │
                                     ├─ kismet_*
                                     ├─ wireshark_*
                                     ├─ ha_*
                                     └─ future project namespaces only when justified
                                             │
                                             └────────> Native APIs
```

Applications may mix all paths in the same module.

Example:

```c
kismet_wifi_scan_passive(&scan_cfg, &stats);

wireshark_80211_parse_mgmt(frame, frame_len, &mgmt);

/* Direct native control remains legal. */
esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

/* Native LVGL remains the UI API. */
lv_label_set_text_fmt(label, "%u APs", stats.emitted_records);
```

---

## 2. Architecture constitution

These rules are the development baseline for this branch.

1. **ESP-IDF, FreeRTOS, LVGL, NimBLE, lwIP, and other native platform APIs remain first-class public APIs for applications.**
2. **Do not wrap a native API only to rename it.**
3. **Board/BSP code exists only for board-specific hardware knowledge, shared-bus topology, and bring-up.**
4. **Level-2 public APIs are organized by upstream project identity when that project contributed a real reusable capability.**
5. **Do not create an API namespace merely because a project was researched or referenced.**
6. **A project namespace exists only when the current firmware already contains a useful implementation, or the capability can be migrated with low cost and has clear application value.**
7. **Do not create a universal public `nearby_*` toolbox abstraction that hides the upstream capability lineage.** Existing `nearby_*` names are migration input, not the target public naming scheme.
8. **Multiple project APIs may share one internal implementation.** Public API identity and internal code ownership are separate concerns.
9. **Applications may freely combine native APIs and multiple Level-2 project APIs.**
10. **LVGL remains LVGL.** Do not create a second widget framework mirroring `lv_*`.
11. **FreeRTOS remains FreeRTOS.** Do not create generic task/queue/semaphore wrappers without a concrete reusable workflow reason.
12. **Existing scanner code is migration source material, not an architectural constraint.**
13. **Project attribution, provenance, source revision, and license status must remain documented when behavior is copied, translated, or materially derived from upstream work.**

The public API should answer a practical developer question:

```text
Need to acquire observations?      -> kismet_*
Need to dissect protocol data?     -> wireshark_*
Need HA-style recognition/semantics? -> ha_*
Need raw control?                  -> native APIs
```

A project name is not enough reason to create an API.

---

## 3. Layer 1 — Native platform

Layer 1 is intentionally **not a NearBy abstraction layer**.

Applications and Level-2 modules directly use upstream APIs such as:

```text
ESP-IDF
├─ esp_wifi_*
├─ esp_netif_*
├─ esp_event_*
├─ esp_lcd_*
├─ esp_ieee802154_*
├─ GPIO / SPI / I2C / USB / SD / FATFS
└─ other official IDF components

NimBLE
├─ BLE GAP
├─ BLE GATT
└─ native controller/host structures

lwIP
├─ sockets
├─ UDP / TCP
└─ network primitives

FreeRTOS
├─ xTaskCreate / vTaskDelete
├─ Queue
├─ Semaphore / Mutex
├─ EventGroup
└─ Timer

LVGL
├─ lv_obj_*
├─ lv_label_*
├─ lv_button_*
├─ lv_event_*
├─ lv_timer_*
└─ the rest of upstream LVGL
```

### What belongs in the BSP

The target board contains facts that upstream ESP-IDF cannot know automatically, including pin assignments, shared buses, display geometry, reset/power sequencing, and touch/display wiring.

Those facts belong in a narrow BSP.

Target BSP responsibilities:

```text
BSP
├─ board pin definitions
├─ shared SPI/I2C initialization
├─ LCD bring-up
├─ touch-controller bring-up
├─ SD-card attachment
├─ backlight / board power details
└─ access to native ESP-IDF handles
```

The BSP should expose native handles whenever possible rather than inventing replacement handle types.

It must not contain scanner semantics, protocol parsing, recognition logic, packet inventory, or application UI behavior.

---

## 4. Layer 2 — Project APIs

Level 2 contains reusable higher-level capabilities migrated from the previous firmware and from the mature projects that informed it.

The target is **not** “one namespace per researched project”.

The target is:

> Keep only the project namespaces that map cleanly to real, reusable capabilities in this firmware.

### First-stage public firmware API families

The first refactor stage intentionally starts with only three project API families:

```text
kismet_*       acquisition / scanning / observation / dedup
wireshark_*    bounded protocol parsing / dissection
ha_*           discovery semantics / recognition / Device-Entity-State model
```

These three families map directly onto substantial functionality that already exists in the previous firmware.

### Host/tool family

Scapy remains a host-side testing/reference tool rather than an ESP32 runtime API:

```text
tools/scapy/
├─ Wi-Fi packet vectors
├─ BLE packet vectors
├─ mDNS / DHCP vectors
├─ IEEE 802.15.4 vectors
└─ Zigbee vectors
```

---

## 5. Kismet API family — acquire observations

The Kismet family owns reusable finite scanning, observation collection, scan statistics, bounded deduplication, and related acquisition workflows.

It should make a common scan easy without hiding the native platform from advanced applications.

### Initial target APIs

```c
kismet_wifi_scan_active(...);
kismet_wifi_scan_passive(...);

kismet_ble_scan(...);

kismet_i154_scan(...);

kismet_mdns_scan(...);
kismet_ssdp_scan(...);
kismet_dhcp_scan(...);

kismet_dedup_init(...);
kismet_dedup_reset(...);
kismet_wifi_dedup(...);
kismet_ble_dedup(...);
kismet_mdns_dedup(...);
```

The exact signatures should be derived from the existing bounded scan configs, observation callbacks, and statistics structures rather than redesigned from zero.

### Existing implementation basis

The current firmware already contains reusable implementations equivalent to:

```text
nearby_wifi_active_scan_run()
nearby_wifi_passive_scan_run()
nearby_ble_scan_run()
nearby_i154_scan_run()
nearby_zeroconf_scan_run()
nearby_ssdp_scan_run()
nearby_dhcp_scan_run()
```

It also already contains generation-scoped deduplication for:

```text
Wi-Fi active
Wi-Fi passive
BLE
mDNS
SSDP
DHCP
```

These are migration candidates, not speculative APIs.

### What Kismet does not own

Kismet does not own raw ESP-IDF radio lifecycle APIs simply because scanning uses them.

Bad:

```c
esp_err_t kismet_wifi_stop(void)
{
    return esp_wifi_stop();
}
```

Good:

```c
esp_err_t kismet_wifi_scan_passive(
    const kismet_wifi_passive_scan_config_t *config,
    kismet_wifi_passive_scan_stats_t *stats);
```

A useful Kismet workflow may coordinate:

```text
current radio state
-> resource lease
-> channel sweep
-> bounded callback handoff
-> observation collection
-> deduplication
-> statistics
-> cleanup / state restoration
```

### Whole-product scan policy stays in the App

The previous firmware hard-coded one seven-module pass:

```text
Wi-Fi Active
Wi-Fi Management
Zeroconf
SSDP
DHCP
BLE
IEEE 802.15.4
```

That fixed product sequence should **not** become a mandatory platform API.

Instead, applications compose the primitive scan APIs they actually need:

```c
static void scanner_app_run(void)
{
    kismet_wifi_scan_active(...);
    kismet_wifi_scan_passive(...);
    kismet_ble_scan(...);
    kismet_mdns_scan(...);
    kismet_ssdp_scan(...);
    kismet_i154_scan(...);
}
```

Ordering, enabled modules, progress presentation, touch locking, and product-level cancellation behavior belong to the application unless a genuinely reusable workflow later emerges.

---

## 6. Wireshark API family — dissect protocol data

The Wireshark family owns small, bounded protocol-dissection helpers that convert captured bytes into structured facts.

The previous firmware already contains MCU-oriented parsers with fixed storage limits and malformed/partial handling. These should be migrated rather than discarded.

### 802.11

Initial target:

```c
wireshark_80211_parse_mgmt(...);
```

The existing parser already handles useful Beacon / Probe Request / Probe Response facts including:

```text
source / destination / BSSID
SSID / hidden SSID
channel
beacon interval
capability
supported rates
RSN
HT
HE
WPS
vendor IEs / OUI
truncation / partial input
```

Useful convenience helpers may be added only when they eliminate repeated application logic, for example:

```c
wireshark_80211_find_ie(...);
wireshark_80211_get_ssid(...);
wireshark_80211_get_channel(...);
wireshark_80211_get_security(...);
```

### BLE

Initial targets:

```c
wireshark_ble_parse_adv(...);
wireshark_ble_find_manufacturer_data(...);
wireshark_ble_find_service_data(...);
wireshark_ble_has_service_uuid(...);
wireshark_ble_uuid_format(...);
```

The current parser already extracts bounded BLE discovery data including:

```text
flags
local name
service UUIDs
service data
manufacturer company ID/data
TX power
appearance
partial/incomplete state
```

Native observation metadata such as RSSI, address type, SID, PHY, and data status should remain available to applications rather than being discarded.

### LAN discovery protocols

Initial targets:

```c
wireshark_mdns_parse(...);
wireshark_ssdp_parse(...);
wireshark_dhcp_parse(...);
```

The current implementation already supports discovery-relevant subsets such as:

```text
mDNS / DNS-SD
├─ compressed names
├─ PTR
├─ SRV
├─ TXT
├─ A
└─ AAAA

SSDP
├─ ST
├─ NT
├─ USN
├─ SERVER
├─ LOCATION
├─ MAN
└─ MX

DHCP
├─ message type
├─ chaddr
├─ hostname
├─ vendor class
└─ client identifier
```

### IEEE 802.15.4 and Zigbee

Initial targets:

```c
wireshark_i154_parse_mac(...);
wireshark_zigbee_parse(...);
```

The existing code already parses useful IEEE 802.15.4 MAC facts and a bounded Zigbee discovery subset including NWK/APS information, endpoint/profile/cluster data, Device Announce, descriptors, manufacturer, and model information.

### Shared implementation is allowed

Wireshark public APIs may reuse internal parsers that are also called from Kismet acquisition workflows.

For example:

```text
kismet_wifi_scan_passive()
        │
        └── internal 802.11 parser
                  │
                  └── same implementation exposed through
                      wireshark_80211_parse_mgmt()
```

Do not duplicate parser code only to preserve namespace identity.

---

## 7. Home Assistant API family — understand devices

The Home Assistant family owns the semantic layer: normalized discovery meaning, device recognition, and the RAM-bounded Device / Entity / State model already implemented in the previous firmware.

Conceptually:

```text
Kismet
acquire observations
        ↓
Wireshark
parse protocol data
        ↓
Home Assistant
recognize / model device semantics
```

Applications are not required to use all three stages.

### Recognition APIs

The previous firmware already has typed recognition paths for BLE, Zeroconf, SSDP, and DHCP backed by the SD recognition database.

Initial target APIs:

```c
ha_match_ble(...);
ha_match_zeroconf(...);
ha_match_ssdp(...);
ha_match_dhcp(...);
```

Low-cost extensions based on already existing recognition key support may include:

```c
ha_match_zigbee(...);
ha_match_matter(...);
ha_match_oui(...);
```

Recognition must preserve fail-closed ambiguity semantics:

```text
MATCHED
AMBIGUOUS
NOT_FOUND
```

`AMBIGUOUS` is not a successful product identity.

### Matter discovery semantics

The current firmware already derives Matter discovery facts from BLE and mDNS.

Because this is currently discovery/recognition semantics rather than a full Matter packet dissector, it belongs with HA-facing semantics for now:

```c
ha_matter_from_ble(...);
ha_matter_from_mdns(...);
ha_match_matter(...);
```

Do not expose a misleading `wireshark_matter_parse()` until the firmware actually contains a meaningful Matter protocol dissector.

### Device / Entity / State runtime

The existing `ha_core` API is already close to the desired public Level-2 API and should largely retain its current naming:

```c
ha_device_upsert(...);
ha_device_remove(...);
ha_device_get(...);
ha_device_get_by_identifier(...);
ha_device_get_by_connection(...);

ha_entity_upsert(...);
ha_entity_remove(...);
ha_entity_get(...);
ha_entity_get_by_unique_id(...);

ha_state_set(...);
ha_state_get(...);

ha_entity_supports_service(...);
ha_entity_call_service(...);
```

Home Assistant semantics are optional application capabilities, not a mandatory global data model for every App.

A Wi-Fi Analyzer or Packet Inspector may never create an HA Device at all.

---

## 8. Upstream projects that remain reference-only for now

The previous project studied or referenced more projects than the initial public API surface needs.

Do **not** create empty or duplicate namespaces merely to represent all references.

| Project | Previous project value | Initial refactor decision |
|---|---|---|
| Home Assistant Core | discovery semantics, generated recognition matchers, Device/Entity/State model | **Public `ha_*` APIs** |
| Kismet | RF inventory, scan/observation semantics | **Public `kismet_*` APIs** |
| Wireshark | dissector field truth, malformed/partial behavior | **Public `wireshark_*` APIs** |
| Scapy | packet schema, test vectors, host-side generation | **Host/test tool only** |
| Bettercap | scanner lifecycle / channel scheduling concepts | reference/internal behavior for now |
| Aircrack-ng | airodump-style 802.11 management parsing reference | represented by current Wi-Fi parser; no namespace yet |
| hcxdumptool | passive 802.11 management/IE parsing reference | represented by current Wi-Fi parser; no namespace yet |
| BtleJack | BLE packet/scan behavior reference | represented by Kismet + Wireshark BLE paths; no namespace yet |
| nRF Sniffer | BLE report/PHY/SID/data-status behavior reference | represented by Kismet + Wireshark BLE paths; no namespace yet |
| Responder | LAN discovery packet identification reference | represented by LAN parsers; no namespace yet |
| mitmproxy | HTTP message-model research | reference-only; no runtime API yet |

A new project namespace should be introduced later only when it gains a distinct reusable capability that is not already cleanly represented by the existing API families.

Examples of future reasons that could justify new namespaces:

```text
Aircrack-ng
-> AP/station inventory aggregation with meaningful independent behavior

hcxdumptool
-> a reusable continuous Wi-Fi capture engine with capture-specific semantics

BtleJack
-> real BLE connection/channel following supported on target hardware

Responder
-> reusable passive LAN host fingerprinting across multiple protocols

mitmproxy
-> a real bounded HTTP message/flow parser useful to applications
```

Until then, keep them as provenance/reference sources.

---

## 9. Recognition data sources are not automatically runtime API namespaces

The recognition database in the previous firmware also uses or studies data from projects such as:

```text
Home Assistant generated discovery tables
ZHA / zha-device-handlers
Zigbee2MQTT / zigbee-herdsman-converters
Nordic Bluetooth numbers database
zigbee-herdsman definitions
SmartThings Matter fingerprints
IEEE OUI data
```

These sources may contribute matcher data, product fingerprints, assigned numbers, or generator inputs without requiring firmware APIs such as `z2m_*` or `zha_*`.

Runtime APIs should be created based on executable capability, not merely based on data provenance.

Source provenance and licensing remain recorded in the database generation pipeline and documentation.

---

## 10. Application layer

Applications are the actual product experiences shown to the user.

Examples:

```text
Scanner
Wi-Fi Analyzer
BLE Explorer
IEEE 802.15.4 Explorer
Nearby Devices
HA Browser
Packet Inspector
Settings
Web Management
```

An application may use only native APIs:

```c
esp_wifi_scan_start(NULL, true);

lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Scanning...");
```

Or one Level-2 family:

```c
kismet_wifi_scan_passive(&config, &stats);
```

Or compose multiple families:

```text
BLE Explorer

Kismet scan
   ↓
Wireshark BLE parse
   ↓
raw parsed advertisement shown in LVGL
```

```text
Nearby Devices

Kismet scan
   ↓
Wireshark parse
   ↓
HA recognition
   ↓
HA Device / Entity / State
   ↓
LVGL
```

### Product workflows stay in applications

The following are generally **application behavior**, not reusable platform APIs:

```text
which scanners run
scan order
one complete product scan pass
progress UI policy
touch locking during scan
home cards
detail screens
settings navigation
which observations become HA Devices
when stale scan generations disappear
Web Management page flow
```

If a workflow later proves reusable across multiple applications, it may be promoted deliberately.

---

## 11. Resource ownership and coexistence

Opening both native and Level-2 API paths means resource ownership must be explicit.

Shared resources include at least:

- the ESP32-C6 2.4 GHz radio;
- Wi-Fi driver state;
- NimBLE host/controller state;
- IEEE 802.15.4 receive state;
- the LCD/SD shared SPI bus;
- SD filesystem access;
- LVGL/UI task ownership.

Level-2 APIs must not assume they own the entire device.

A high-level helper should, where practical:

```text
inspect current state
        ↓
acquire the minimum required lease/lock
        ↓
perform the workflow
        ↓
restore previous state
        ↓
release the lease/lock
```

A small internal resource coordinator may exist for ownership, leases, mutexes, cancellation, and conflict control.

It must **not** become another hardware abstraction API.

Good internal mechanism:

```text
resource_acquire(WIFI)
resource_release(WIFI)
```

Bad public abstraction:

```text
resource_wifi_scan()
resource_ble_start()
```

The latter simply recreates another wrapper layer.

---

## 12. Target source layout

The tree will migrate toward a structure similar to:

```text
firmware/
├─ components/
│  ├─ bsp/
│  │  └─ waveshare_esp32c6_touch_lcd_1_9/
│  │
│  ├─ lvgl_port/
│  │
│  ├─ toolbox/
│  │  ├─ kismet/
│  │  │  ├─ wifi/
│  │  │  ├─ ble/
│  │  │  ├─ ieee802154/
│  │  │  ├─ lan/
│  │  │  └─ dedup/
│  │  │
│  │  ├─ wireshark/
│  │  │  ├─ ieee80211/
│  │  │  ├─ ble/
│  │  │  ├─ mdns/
│  │  │  ├─ ssdp/
│  │  │  ├─ dhcp/
│  │  │  ├─ ieee802154/
│  │  │  └─ zigbee/
│  │  │
│  │  └─ home_assistant/
│  │     ├─ core/
│  │     ├─ discovery/
│  │     └─ recognition/
│  │
│  ├─ internal/
│  │  ├─ radio/
│  │  ├─ packet/
│  │  ├─ resource/
│  │  └─ common/
│  │
│  └─ app_runtime/
│
└─ main/
   └─ main.c

apps/
├─ scanner/
├─ wifi_analyzer/
├─ ble_explorer/
├─ nearby_devices/
├─ packet_inspector/
└─ ...

tools/
└─ scapy/
```

Public dependency direction:

```text
App
├────────────────────────────> Native APIs
│
├──> kismet_* ────────────────> Native APIs
├──> wireshark_* ─────────────> Native APIs / bounded C helpers
└──> ha_* ────────────────────> bounded semantic/recognition runtime
```

Internal code may be shared between project APIs to avoid duplication.

---

## 13. Migration from the current firmware

The existing integrated scanner firmware contains valuable tested behavior. It should be migrated rather than discarded wholesale.

Old component boundaries and old `nearby_*` public names are not automatically preserved.

### Current component mapping

| Current area | New direction |
|---|---|
| `nearby_board` | narrow BSP / board bring-up |
| `nearby_lvgl_port` | thin `lvgl_port` |
| `radio_runtime` | remove rename-wrapper role; native lifecycle calls stay native; useful orchestration moves into Kismet/internal resource code |
| `native_handoff` | retain useful bounded callback-to-task handoff internally; do not expose as a universal packet ABI |
| `scan_coordinator` | split reusable scan mechanics from Scanner App fixed product policy |
| `scan_session` | migrate useful lease/fatal-cleanup mechanics into internal resource/Kismet workflow support |
| `discovery_parser` | split into `wireshark_*` parsers, `kismet_*` acquisition, and `ha_*` discovery/recognition adapters |
| `recognition_db` | retain bounded SD-backed recognition implementation; expose product-facing matching through `ha_*` where appropriate |
| `ha_core` | retain and stabilize as `ha_*` semantic APIs |
| `nearby_scan_pipeline` | mostly application composition; reusable pieces migrate down |
| `nearby_semantic_apply` | application glue, not a mandatory platform API |
| `nearby_ui` / product screens | application layer using native LVGL |
| `main.c` | reduce to boot/platform/app-runtime startup |

### Existing function -> target API migration examples

| Existing implementation | Target public direction |
|---|---|
| `nearby_wifi_active_scan_run()` | `kismet_wifi_scan_active()` |
| `nearby_wifi_passive_scan_run()` | `kismet_wifi_scan_passive()` |
| `nearby_ble_scan_run()` | `kismet_ble_scan()` |
| `nearby_i154_scan_run()` | `kismet_i154_scan()` |
| `nearby_zeroconf_scan_run()` | `kismet_mdns_scan()` |
| `nearby_ssdp_scan_run()` | `kismet_ssdp_scan()` |
| `nearby_dhcp_scan_run()` | `kismet_dhcp_scan()` |
| `nearby_wifi_parse_management_frame()` | `wireshark_80211_parse_mgmt()` |
| `nearby_ble_parse_advertisement()` | `wireshark_ble_parse_adv()` |
| `nearby_mdns_parse()` | `wireshark_mdns_parse()` |
| `nearby_ssdp_parse()` | `wireshark_ssdp_parse()` |
| `nearby_dhcp_parse()` | `wireshark_dhcp_parse()` |
| `nearby_i154_parse_mac()` | `wireshark_i154_parse_mac()` |
| `nearby_zigbee_parse_from_mac()` | `wireshark_zigbee_parse()` |
| `nearby_recognition_match_ble()` | `ha_match_ble()` |
| `nearby_recognition_match_zeroconf()` | `ha_match_zeroconf()` |
| `nearby_recognition_match_ssdp()` | `ha_match_ssdp()` |
| `nearby_recognition_match_dhcp()` | `ha_match_dhcp()` |
| `nearby_matter_from_ble()` | `ha_matter_from_ble()` |
| `nearby_matter_from_mdns()` | `ha_matter_from_mdns()` |
| existing `ha_device_* / ha_entity_* / ha_state_*` | retain/stabilize |

This table is a migration baseline, not a requirement to mechanically rename everything before boundaries are cleaned up.

---

## 14. Internal callback handoff remains valuable

The previous firmware already contains bounded callback handoff for Wi-Fi promiscuous RX, NimBLE legacy/extended discovery reports, and IEEE 802.15.4 receive frames.

That behavior is useful because native callbacks often cannot safely perform all parsing/application work directly.

The refactor should preserve the useful mechanics:

```text
native callback
-> bounded copied record
-> queue/ring
-> worker/task
-> parser / application callback
```

But the internal record types must remain transport-specific. Do not invent a universal cross-radio packet ABI merely to unify Wi-Fi, BLE, and IEEE 802.15.4.

---

## 15. Provenance and licensing

Project-organized APIs make provenance especially visible, but the public namespace alone is not sufficient legal/source documentation.

For materially copied, translated, adapted, or generated behavior, preserve as appropriate:

```text
upstream project
repository URL
immutable revision/tag
source path
license
classification:
  copied
  modified
  translated
  generated data
  reference-only
```

GPL or unclear-license sources may remain reference/test or behavioral guidance unless the project's distribution policy explicitly permits incorporation.

A shared implementation influenced by several projects should document all material sources rather than pretending one public namespace is the sole provenance.

---

## 16. Hardware baseline

Target board: **Waveshare ESP32-C6-Touch-LCD-1.9**

Current verified baseline:

| Function | GPIO / detail |
|---|---|
| LCD resolution | 170 × 320 portrait |
| LCD controller | ST7789V2 baseline |
| LCD MOSI / SD MOSI | GPIO4 |
| LCD SCLK / SD CLK | GPIO5 |
| LCD DC | GPIO6 |
| LCD CS | GPIO7 |
| LCD RST | GPIO14 |
| LCD backlight | GPIO15 |
| SD MISO | GPIO19 |
| SD CS | GPIO20 |
| I2C SCL | GPIO8 |
| I2C SDA | GPIO18 |
| Touch | CST816, address `0x15` |
| MCU target | `esp32c6` |
| Flash | 8 MB board baseline |
| PSRAM | none assumed |

LCD and SD share `SPI2_HOST`, so the BSP must initialize and own the bus topology coherently rather than allowing independent drivers to repeatedly create/free the same bus.

---

## 17. Build baseline

The firmware remains a normal ESP-IDF project.

Typical build flow:

```bash
cd firmware
idf.py set-target esp32c6
idf.py build
```

Flash/monitor:

```bash
idf.py -p <PORT> flash monitor
```

The architecture reset should remain buildable throughout migration. Large rewrites should be split into stages rather than leaving the branch broken for long periods.

---

## 18. Development phases

### Phase 0 — Architecture baseline

This branch begins here.

- establish Native + project-organized Level-2 API architecture;
- replace the old single-scanner assumptions;
- define first-stage `kismet_*`, `wireshark_*`, and `ha_*` boundaries;
- preserve other researched projects as provenance/reference until they justify independent APIs;
- keep existing firmware as migration input.

### Phase 1 — Native foundation cleanup

- reduce BSP to board-specific responsibilities;
- remove redundant ESP-IDF/FreeRTOS/LVGL wrappers;
- keep LVGL port thin;
- define shared resource/ownership rules;
- retain useful bounded callback handoff internally;
- shrink `main/`.

### Phase 2 — Kismet extraction

- migrate Wi-Fi active/passive acquisition;
- migrate BLE acquisition;
- migrate IEEE 802.15.4 acquisition;
- migrate mDNS/SSDP/DHCP finite scans;
- migrate bounded scan dedup/statistics;
- keep whole-product scan ordering in applications.

### Phase 3 — Wireshark extraction

- migrate 802.11 management parser;
- migrate BLE AD parser/helpers;
- migrate mDNS/SSDP/DHCP parsers;
- migrate IEEE 802.15.4 MAC parser;
- migrate Zigbee discovery parser;
- preserve malformed/partial/bounds behavior and host vectors.

### Phase 4 — Home Assistant semantic extraction

- stabilize existing Device/Entity/State API;
- migrate BLE/Zeroconf/SSDP/DHCP typed recognition into `ha_*` entry points;
- add low-cost Zigbee/Matter recognition paths backed by already available DB keys/data;
- keep HA semantics optional per application.

### Phase 5 — App runtime

- define application lifecycle;
- move scanner product flow into an application;
- create additional applications using native, Kismet, Wireshark, HA, or mixed APIs;
- build navigation/launcher behavior appropriate for the handheld device.

### Phase 6 — Expand only when capability exists

Future project namespaces may be added for Bettercap, Aircrack-ng, hcxdumptool, BtleJack, nRF Sniffer, Responder, mitmproxy, or other projects **only after** a distinct reusable capability exists and is useful to applications.

Do not create placeholder APIs.

---

## 19. Security boundary

NearBy One NEXT is intended for legitimate device discovery, diagnostics, experimentation, development, protocol analysis, and authorized interaction.

The baseline may support passive observation, protocol parsing, discovery, packet capture where legally/technically appropriate, and user-authorized device interaction.

Do not add features whose primary purpose is credential theft, unauthorized access, persistence on third-party devices, destructive interference, or bypassing access controls.

Reference to a security tool does not imply that all upstream offensive capabilities belong in NearBy One NEXT.

---

## 20. Definition of a good Level-2 API

Before adding a public API, ask:

```text
1. Is this already directly available from ESP-IDF / FreeRTOS / LVGL / NimBLE / lwIP?
   └─ If yes and we are only renaming it: do not add the wrapper.

2. Does the previous firmware already contain this capability, or can it be migrated cheaply?
   └─ If no: do not create a placeholder API.

3. Is the capability independently useful to more than one possible application?
   └─ If no: keep it in the App.

4. Does one upstream project provide the clearest public semantic identity for this capability?
   └─ If yes: use that project namespace.

5. Is the implementation shared with another project API?
   └─ That is fine. Share internal code; do not duplicate it.
```

A good Level-2 API normally adds at least one of:

- multi-step orchestration;
- bounded acquisition;
- resource arbitration;
- state preservation/restoration;
- channel scheduling;
- packet/field parsing;
- malformed/partial handling;
- normalization;
- deduplication;
- statistics;
- typed recognition;
- reusable semantic modeling;
- composition of multiple native subsystems.

---

## 21. Baseline status

This README describes the **target architecture of the refactor branch**.

The source tree is currently in transition. Existing `nearby_*` components still implement much of the previous integrated scanner architecture until they are migrated, renamed, split, or removed.

For new development on this branch:

> **Native APIs are the foundation. Level-2 APIs keep useful upstream project identity. Kismet acquires, Wireshark dissects, Home Assistant understands. Applications compose only what they need.**

That is the NearBy One NEXT refactor baseline.
