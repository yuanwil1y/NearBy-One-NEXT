# NearBy One NEXT

Portable, standalone nearby-device discovery and interaction panel for the **Waveshare ESP32-C6-Touch-LCD-1.9**, built with **ESP-IDF**.

> **This README is the canonical project specification.**
>
> It records the product decisions, architecture, hardware assumptions, runtime rules, UI behavior, recognition-database model, security scope, Agent ownership boundaries, and validation requirements agreed for NearBy One NEXT as of 2026-09-06.
>
> Branch-local `AGENTS.md` files refine execution inside one Agent's scope, but they must not contradict or broaden this README. When an old branch document conflicts with this README, this README wins until an explicit later project decision changes it.

---

## 1. Product definition

NearBy One NEXT is a **portable, transient nearby-device browser/controller**. The intended feel is roughly:

```text
Home Assistant Dashboard × AirDrop × Find My × universal remote
```

The user carries one ESP32-C6 device, presses **Scan**, sees real-world nearby devices as simple cards, opens one Device, and sees Home Assistant-style Entities and controls where interaction is supported.

This is **not** a fixed-home Home Assistant server. It is not intended to reproduce HA's whole backend, persistence model, dashboards, automation engine, history database, or configuration system.

The product principle is:

> Reuse Home Assistant and mature upstream protocol/scanner behavior as directly as practical; write only the MCU-specific glue that cannot be reused.

Prefer:

- upstream field names and semantics;
- generated tables rather than hand-maintained product lists;
- thin translation glue rather than new frameworks;
- fixed/bounded MCU representations rather than Python/server machinery;
- native ESP-IDF/NimBLE structures rather than a new generic packet model.

Avoid inventing NearBy-specific abstractions when Home Assistant already has a concept for the same thing.

---

## 2. Non-negotiable architecture

The semantic architecture is:

```text
Protocol / Discovery
        ↓
Integration / Platform semantics
        ↓
Device + Entity + State
        ↓
UI
```

The implementation work is split as:

```text
Agent D
Hardware / ESP-IDF / native RF+network input
        ↓
Agent B
Scanner / protocol parsing / HA-compatible discovery facts
        ↓
Agent C
Recognition database / matcher knowledge / product metadata
        ↓
Agent A
RAM-only Home Assistant Device / Entity / State semantic core
        ↓
Agent E
Generic LVGL UI + Web Management UX
```

### Hard semantic rule

**Entity is the capability model. There is no additional Capability layer.**

Do not introduce any of the following as a product abstraction:

- `Capability`
- `Action` framework
- `ViewModel` framework
- `Provider` framework
- generic `Adapter` hierarchy
- proprietary Device Graph
- universal action schema
- custom domain system parallel to Home Assistant

Use Home Assistant concepts directly:

- Device
- Entity
- State
- attributes
- domain
- device_class
- supported_features
- HA-style services

Examples:

```text
Physical temperature sensor
└─ Device
   ├─ sensor.temperature
   ├─ sensor.humidity
   └─ sensor.battery
```

```text
Physical smart light
└─ Device
   └─ light.main
      ├─ state: on/off
      ├─ brightness attribute
      └─ light.turn_on / light.turn_off semantics
```

### No proprietary cross-protocol identity fusion in v0.1

Do not build a DeviceGraph that attempts to merge BLE/Wi-Fi/Matter/Zigbee observations into one physical Device through heuristics.

If the same physical product appears through multiple unrelated discovery paths in v0.1, duplicate cards are acceptable until an upstream/HA-compatible identity relationship provides a reliable merge rule.

---

## 3. Persistence policy

The persistence rule is intentionally severe:

> **No runtime product state is persistent. The recognition/matching database on SD is the only product data intentionally persisted.**

Every boot starts as a new nearby-device session.

Do **not** persist:

- discovered Devices;
- Entities;
- States;
- scan results;
- favorites;
- rooms/areas;
- user device organization;
- history;
- statistics;
- dashboard layouts;
- automation/scene state;
- service results;
- runtime identity mappings;
- Wi-Fi credentials in v0.1.

Wi-Fi provisioning is **session-only** in v0.1 unless a later explicit project decision changes this rule.

Firmware, compiled assets, and the static Web Portal may of course live in firmware flash; this rule concerns runtime/user product data.

The SD card is primarily for the large static recognition database, not for runtime state/history.

---

## 4. Explicit non-goals

Do not implement these Home Assistant/server concepts in v0.1:

- Automations
- Scenes
- Areas / Rooms
- persistent dashboards
- recorder/history
- restore-state
- long-term statistics
- templates
- favorites
- saved device organization
- persistent device/entity registry
- HA config-flow/storage machinery
- full HA event bus
- full HA service registry
- Python runtime
- SQLite runtime registry

NearBy One NEXT is a transient portable device, so features whose value depends on a stable home installation are deliberately excluded.

---

## 5. Target hardware

### Board

**Waveshare ESP32-C6-Touch-LCD-1.9**

Target framework: **ESP-IDF**, not Arduino.

Known board capabilities/baseline:

- MCU: ESP32-C6FH8, RISC-V, up to 160 MHz
- Flash: 8 MB
- HP SRAM: 512 KB
- LP SRAM: 16 KB
- **No PSRAM documented for this board; design as no-PSRAM**
- LCD: 1.9-inch, 170×320 portrait
- LCD controller: **ST7789V2** according to Waveshare hardware/docs/Arduino path
- Touch controller: **CST816**
- microSD / TF slot; current development card: 1 GB
- IMU: QMI8658
- 2.4 GHz Wi-Fi 6 (`802.11ax/b/g/n`)
- Bluetooth Low Energy 5; **no Bluetooth Classic**
- IEEE 802.15.4 for Thread/Zigbee-class operation
- one shared 2.4 GHz RF subsystem, so Wi-Fi/BLE/802.15.4 need explicit scheduling/arbitration
- touch SKU exposes an IPEX1 antenna option; board solder-pad configuration must be checked before depending on the external antenna path

### Verified/settled pin baseline

| Function | GPIO | Notes |
|---|---:|---|
| LCD MOSI / SD MOSI | 4 | shared SPI2 wire |
| LCD SCLK / SD CLK | 5 | shared SPI2 wire |
| LCD DC | 6 | LCD only |
| LCD CS | 7 | LCD only |
| LCD RST | 14 | LCD only |
| LCD backlight | 15 | board transistor path; current D baseline treats logical low as ON |
| SD MISO | 19 | SDSPI input |
| SD CS | 20 | SD only |
| I2C0 SCL | 8 | touch/IMU bus |
| I2C0 SDA | 18 | touch/IMU bus |
| QMI8658 INT1 | 1 | IMU |
| QMI8658 INT2 | 2 | IMU |
| USB D- | 12 | native USB; avoid reuse while USB active |
| USB D+ | 13 | native USB; avoid reuse while USB active |
| UART0 TX | 16 | debug/serial |
| UART0 RX | 17 | debug/serial |
| BOOT | 9 | boot key |
| BAT ADC | 0 | battery sensing |

### Shared SPI rule

LCD and SD share `SPI2_HOST` MOSI/CLK.

Therefore:

- initialize SPI2 **once**;
- include MISO GPIO19 from the beginning, even if LCD is initialized first;
- attach LCD and SDSPI to the same initialized bus;
- use independent CS lines GPIO7/GPIO20;
- never independently free/reinitialize SPI2 while either LCD or SD is attached;
- rely on ESP-IDF SPI/SDSPI transaction serialization first;
- only add higher-level LCD/SD throttling after measurement proves it is needed.

---

## 6. LCD controller decision

Waveshare's current product documentation and Arduino example identify the panel as **ST7789V2**. A Waveshare ESP-IDF LVGL example currently names an `esp_lcd_sh8601` wrapper, but that wrapper uses an ST7789-like initialization sequence and conflicts with the product documentation.

The canonical engineering direction is therefore:

```text
SPI2_HOST
  -> esp_lcd_new_panel_io_spi()
  -> esp_lcd_new_panel_st7789()
  -> esp_lcd_panel_reset()
  -> esp_lcd_panel_init()
  -> Waveshare-specific board tuning commands
  -> esp_lcd_panel_set_gap(panel, 35, 0)
  -> inversion setting per Waveshare ESP-IDF sequence
  -> display on
```

Board-specific tuning currently preserves the Waveshare ESP-IDF register values for:

```text
B2 B7 BB C0 C2 C3 C4 C6 D0 D6 E0 E1
```

Visible geometry is **170×320 RGB565**, with controller X gap **35**, Y gap **0** in portrait baseline.

Do not carry the misleading `SH8601` wrapper into the product just for its name; retain only the board-specific values/behavior that the physical ST7789 path needs.

### BENCH_REQUIRED

Before marking LCD support physically verified, run a real-board acceptance test:

1. ST7789 public driver + Waveshare tuning;
2. `(35,0)` gap;
3. distinct four-corner pixels/rectangles;
4. grayscale/color ramp for gamma/inversion;
5. backlight on/off;
6. CST816 four-corner touch coordinates;
7. SD mount on the same already-initialized SPI2 bus.

Real hardware is the final authority if documentation/examples conflict.

---

## 7. Memory policy

The ESP32-C6 target has no PSRAM safety net. RAM is shared by:

- LVGL objects and draw buffers;
- Wi-Fi stack/buffers;
- NimBLE host/controller;
- IEEE 802.15.4;
- FreeRTOS tasks/stacks;
- FATFS/SDSPI;
- HTTP/Web Management;
- scanner handoff rings;
- Device/Entity/State runtime;
- integration/parser working buffers.

Therefore every layer must be bounded and measured.

Rules:

- no per-packet heap allocation in RF callbacks/ISR paths;
- no full recognition DB in RAM;
- no full DB shard decode as the primary lookup design;
- no huge JSON DOM on device;
- no `std::map`/large dynamic object graphs for the semantic core;
- fixed capacities must be compile-time tunable;
- capacity exhaustion must fail explicitly, never corrupt state silently;
- transient UI screens should be deleted/reused instead of keeping multiple large object trees resident;
- Agent A must report `sizeof()` and total default Device/Entity/State pool RAM;
- Agent D must measure real heap/stack/init/deinit/switch costs on hardware;
- Agent C's install/lookup buffers should remain bounded; target upload/install buffer is <=16 KiB.

A previous first-pass A configuration with 24 Devices / 96 Entities / 96 States and large fixed strings was roughly in the ~200 KB static-RAM class and is **not an acceptable final default**. Preserve HA semantics while compacting representation and realistic capacities.

---

## 8. Radio and native-input contract

Agent D owns the actual ESP-IDF driver lifecycle and native callback ownership rules. Agent B consumes those native observations and performs scanning/parsing.

Do **not** invent one universal `packet_t` across Wi-Fi/BLE/802.15.4. Their native metadata and lifetime rules are materially different.

### 8.1 Wi-Fi active scan

Use ESP-IDF APIs such as:

```c
esp_wifi_scan_start(...)
esp_wifi_scan_get_ap_num(...)
esp_wifi_scan_get_ap_records(...)
```

B may consume `wifi_ap_record_t` while the application-owned record array is alive.

### 8.2 Wi-Fi promiscuous RX

Use:

```c
esp_wifi_set_promiscuous_filter(...)
esp_wifi_set_promiscuous_rx_cb(...)
esp_wifi_set_promiscuous(true)
esp_wifi_set_channel(...)
```

Important rules:

- callback runs in the Wi-Fi driver task, not a hardware ISR;
- keep callback short/nonblocking;
- treat `wifi_promiscuous_pkt_t *` and payload as callback-scoped driver-owned memory;
- never queue the original driver pointer;
- copy only required native metadata + bounded frame bytes into a preallocated transport-specific ring/queue;
- track drops/truncation explicitly;
- do not parse protocols, log every packet, touch LVGL, or perform SD I/O inside the callback.

### 8.3 ESP-NimBLE discovery

Use native GAP discovery events, e.g. legacy `BLE_GAP_EVENT_DISC` and the relevant extended-advertising event path when enabled.

Rules:

- GAP callback runs in NimBLE host task/event context;
- `ble_gap_event`, discovery descriptors, advertisement data, and pointer fields from `ble_hs_adv_parse_fields()` are callback-scoped;
- if parsing occurs later, copy address/type, RSSI/scalars, and exact bounded advertisement bytes;
- do not retain `event`, `event->disc.data`, or parsed pointer fields past callback return.

### 8.4 IEEE 802.15.4

Use native ESP-IDF radio APIs and callback:

```c
void esp_ieee802154_receive_done(
    uint8_t *frame,
    esp_ieee802154_frame_info_t *frame_info);
```

Rules:

- receive callback is ISR-context work;
- frame begins with PHY length byte followed by MHR/MAC payload; FCS is validated by hardware and not treated as a normal exposed trailing FCS payload;
- copy only the length+frame bytes and `esp_ieee802154_frame_info_t` into a preallocated ISR-safe slot;
- call `esp_ieee802154_receive_handle_done(frame)` **before returning from the callback** after copying;
- never queue the original `frame` pointer;
- do no Zigbee/Thread parser work in ISR context;
- wake a task with ISR-safe queue/notification mechanisms;
- track overload drops.

### 8.5 Driver implementation requirement

A handoff queue alone is not considered a completed driver path.

D must provide real start/stop/receive lifecycle for:

```text
Wi-Fi active/promiscuous
NimBLE scanning
IEEE 802.15.4 promiscuous receive/channel traversal
```

and clean error/deinit recovery so the next scan can run again.

---

## 9. Exclusive scan-session rule

ESP32-C6 has one shared 2.4 GHz RF subsystem. The product therefore treats one complete discovery pass as an **exclusive foreground scan session**.

The lower-level lock is authoritative; the UI blocker is only a UX layer.

Lifecycle:

```text
IDLE
  ↓ user presses Scan
acquire scan-session gate
  ↓
SCANNING
  ├─ scanner module 1 completes
  ├─ scanner module 2 completes
  ├─ ...
  └─ every enabled scanner module reaches terminal completion
  ↓
release scan-session gate
  ↓
IDLE
```

Rules:

- acquire the product gate once at full-scan start;
- keep it for the whole scan, not once per radio phase;
- all enabled scanner modules are traversed once per full pass;
- progress is based on actual module completion, **not elapsed wall-clock time**;
- equal progress steps are acceptable for v0.1; do not over-engineer weights;
- if a module is unavailable/skipped/error, it still needs a terminal completion/error event so the session can finish cleanly;
- error/cancel paths must always release the gate;
- device interaction/RF operations during the session return BUSY/invalid-state immediately;
- do not take the product mutex inside RF callbacks/ISR;
- Web Management/provisioning and full nearby scan must not silently overlap as competing Wi-Fi/RF modes;
- if the Web Portal is running, scanning must either be rejected as busy or the portal must be explicitly stopped before acquiring the scan session;
- Entity services must not start competing device communication while scanning.

Agent B owns scanner-level orchestration/module completion. Agent D owns the underlying RF/hardware ownership primitive and real driver lifecycle.

---

## 10. Scan behavior visible to the user

### Boot

**Do not scan automatically on boot.**

Boot lands in an empty/idle Nearby screen.

A small empty-state hint such as `Press scan to start` is acceptable, but the UI should remain minimal.

### Starting a scan

The user presses the fixed top-left Scan button.

A new scan represents a fresh nearby-result generation. v0.1 should not indefinitely accumulate stale Devices from previous scan passes; old current-result cards should be cleared/replaced for the new pass unless later lifecycle logic explicitly defines freshness.

### During scan

- discovered Device cards may appear incrementally;
- the screen continues rendering;
- progress continues updating;
- **all touch interaction is frozen**;
- no Device detail may open;
- Settings may not open;
- scrolling cannot begin;
- another scan cannot start;
- Entity controls/services cannot run.

Recommended LVGL implementation: a full-screen transparent input-blocking object on the top layer that consumes touch while leaving the screen underneath visible and repainting.

### Completion

When all scanner modules finish:

- progress reaches 100%;
- scan session is fully ended/released;
- input blocker is removed;
- progress bar is hidden/reset;
- Scan icon/button returns to normal idle state;
- the discovered Device list stays visible for browsing.

---

## 11. Scanner / parser layer (Agent B)

B converts D's verified native observations into discovery facts and matcher inputs.

### B owns

- Home Assistant Bluetooth discovery behavior;
- Zeroconf/mDNS discovery behavior;
- SSDP discovery behavior;
- DHCP discovery behavior;
- BLE advertisement/service/manufacturer-data parsing;
- Wi-Fi passive management-frame parsing where useful for device discovery;
- IEEE 802.15.4 / Zigbee / Thread / Matter discovery parsing where practical;
- caching/dedup/add-update-remove behavior inspired by HA's scanners;
- host parser test vectors;
- scan-module orchestration and module-completion progress;
- safe reuse/ports from mature scanner projects.

### B does not own

- GPIO/BSP/driver initialization or callback lifetime (D);
- recognition database storage/source ingestion (C);
- Device/Entity/State runtime (A);
- UI (E).

### HA discovery behavior to reuse before inventing glue

Bluetooth references:

```text
homeassistant/components/bluetooth/manager.py
homeassistant/components/bluetooth/api.py
homeassistant/components/bluetooth/match.py
homeassistant/components/bluetooth/models.py
homeassistant/components/bluetooth/passive_update_processor.py
homeassistant/components/bluetooth/active_update_processor.py
```

Zeroconf references:

```text
homeassistant/components/zeroconf/discovery.py
homeassistant/generated/zeroconf.py
script/hassfest/zeroconf.py
```

SSDP references:

```text
homeassistant/components/ssdp/scanner.py
homeassistant/components/ssdp/server.py
homeassistant/components/ssdp/common.py
```

Desired SSDP behavior includes discovery, ALIVE/BYEBYE/UPDATE handling, device tracking, description caching, callbacks, and indexed integration matching where useful on MCU.

DHCP references:

```text
homeassistant/components/dhcp/__init__.py
homeassistant/components/dhcp/helpers.py
homeassistant/components/dhcp/models.py
```

Reuse HA's matcher/index concepts rather than making a NearBy-specific discovery schema when HA already has one.

For network discovery, examples include HomeKit `_hap._tcp.local.` / `_hap._udp.local.` and Matter `_matter._tcp.local.` / `_matterc._udp.local.`.

---

## 12. Recognition database (Agent C)

The SD database is a **large, static, read-only recognition/knowledge base** generated on a PC from pinned upstream sources.

Runtime lookup flow:

```text
D native observation
    ↓
B parses observation / derives HA-compatible discovery facts + matcher keys
    ↓
C read-only DB lookup
    ↓
matched integration/profile/product metadata
    ↓
A receives already-matched/already-parsed HA-like semantic input
    ↓
Device / Entity / State
```

### C owns

- source coverage inventory;
- provenance/license audit;
- reproducible PC generator;
- normalized matcher/product metadata;
- read-only file/container format;
- sorted indexes/sharding/string deduplication;
- DB header/schema/version/build metadata;
- compatibility/integrity checks;
- tiny bounded-RAM lookup reader;
- coverage/conflict/license reports;
- database status/version exposed upward.

### C does not own

- raw radio parsers (B);
- runtime Device/Entity/State (A);
- SD electrical/driver/HTTP transport (D);
- UI (E);
- dynamic parser code execution from SD.

### Source classification

Every source is classified as:

- `DATA_ONLY`
- `DATA+PARSER`
- `CODE_ONLY`

Static searchable knowledge may go to SD. Complex parser/codec logic stays compiled in firmware.

**No plugin VM, dynamic parser language, Lua/Python runtime, or downloaded executable decoder model.**

### Priority knowledge sources

1. Home Assistant generated discovery matchers/data
2. `zigpy/zha-device-handlers` / ZHA quirks
3. Zigbee2MQTT `zigbee-herdsman-converters`
4. static product/parser-selection tables from HA integration dependencies
5. IEEE OUI data
6. Bluetooth SIG assigned numbers
7. Matter VID/PID sources
8. Zigbee manufacturer/profile/model IDs
9. ESPHome Devices only as license-compatible/reference enrichment
10. Theengs Decoder primarily as reference/test oracle unless distribution policy explicitly changes

### License/provenance policy

Each imported source record/build must retain enough metadata to answer:

- source repository/dataset;
- immutable revision/tag/hash/date;
- source file/path where relevant;
- SPDX license or explicit `LicenseRef-*`;
- redistribution state;
- classification;
- generator/transform used;
- whether content is copied, mechanically transformed, generated, or reference-only.

Maintain `THIRD_PARTY.md`.

Current policy direction:

- Home Assistant Core: Apache-2.0 — usable with attribution/provenance;
- ZHA quirks: Apache-2.0;
- Zigbee2MQTT converters: MIT;
- IEEE/Bluetooth SIG/CSA bulk registries: redistribution terms must be reviewed, fail closed for release until allowed;
- ESPHome Devices GPL-family repository content: reference-only by default unless distribution policy is explicitly changed;
- Theengs Decoder GPL-family: reference/test oracle by default;
- HA integration dependencies must be audited individually; HA Core's Apache license does not automatically cover every dependency.

### Known discovery/product examples from HA research

These are recognition/parser research anchors, **not Agent A responsibilities**:

- Xiaomi BLE HA matcher/service UUID examples: `181b`, `181d`, `fd50`, `fe95`; current research pinned `xiaomi-ble==1.16.0` at the time of audit.
- SwitchBot examples: service-data UUIDs `0d00`, `fd3d`, service UUID family `cba20d00...`, manufacturer IDs observed in HA matching data including 2409, 89, 741; current research referenced `PySwitchbot==2.7.0`.
- Shelly examples: local name `Shelly*`, manufacturer ID 2985, Zeroconf patterns including `shelly*` on `_http._tcp.local.` and `_shelly._tcp.local.`; current research referenced `aioshelly==13.32.0`.
- Matter discovery: `_matter._tcp.local.` and `_matterc._udp.local.`; runtime Matter protocol work should prefer Espressif native Matter support where needed.

Pinned versions are reproducibility anchors, not permanent forever-versions; generator builds must record the exact revision used.

---

## 13. Recognition DB distribution format direction

The current chosen v0 direction is a **custom sorted flat binary** rather than on-device SQLite or whole-shard CBOR/MessagePack decode.

Reasoning from the first host benchmark:

- 1 GB SD makes a modest file-size penalty acceptable;
- a flat reader can binary-search fixed index entries and read only candidate key/value bytes;
- no SQLite runtime/page cache/code size;
- no requirement to decode a whole CBOR/MessagePack shard into RAM;
- implementation can remain a small `fseek`/`fread`-style reader over ESP-IDF VFS/FATFS.

SQLite may still be useful as a **PC-side intermediate/build tool**, not as the primary MCU runtime DB.

### Current 128-byte container header contract

All integers are little-endian.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 8 | `magic` | `NBYDB\0\r\n` |
| 8 | 2 | `format_major` | currently `1` |
| 10 | 2 | `format_minor` | additive compatible changes |
| 12 | 2 | `header_size` | `128` |
| 14 | 2 | `flags` | bit 0 = payload SHA-256 present; unsupported mandatory semantics reject |
| 16 | 4 | `schema_version` | normalized recognition schema, currently `1` |
| 20 | 4 | `db_version` | build/version integer |
| 24 | 4 | `min_reader_abi` | minimum firmware DB reader ABI |
| 28 | 4 | `source_count` | source records in manifest |
| 32 | 8 | `file_size` | exact `.nbdb` total bytes |
| 40 | 8 | `manifest_offset` | normally begins after header |
| 48 | 8 | `manifest_size` | bytes |
| 56 | 8 | `payload_offset` | payload start |
| 64 | 8 | `payload_size` | bytes to EOF |
| 72 | 4 | `payload_crc32` | payload CRC32 |
| 76 | 4 | `header_crc32` | header CRC with this field zeroed |
| 80 | 32 | `payload_sha256` | payload SHA-256 when enabled |
| 112 | 8 | `build_epoch` | informational Unix seconds |
| 120 | 8 | `reserved` | zero for current format |

The exact payload encoding/indexes may evolve while retaining container compatibility. Any schema change must be versioned, reproducible, and accompanied by tests.

### Runtime lookup rules

- open DB read-only;
- validate magic/version/header CRC/ranges cheaply;
- binary-search bounded index entries;
- return value references and support chunked value reads;
- never load full DB/full index/full shard into RAM;
- full payload CRC/SHA is primarily an install-time validation responsibility, not a boot-time full-file scan.

### Target index families

Move from a generic PoC string key toward explicit efficient lookup families as real sources mature, including examples such as:

- BLE manufacturer ID;
- BLE service UUID;
- BLE service-data UUID / product key;
- local-name pattern index;
- OUI/prefix;
- Zeroconf service/TXT matcher;
- SSDP header matcher;
- DHCP hostname/OUI matcher;
- Zigbee manufacturer + model;
- Zigbee endpoint/profile/device-type/cluster signatures where declarative;
- Matter VID/PID.

Do not turn parser code into a DB bytecode language.

---

## 14. Database installation is deliberately destructive

This is a settled product rule:

> **Installing a new recognition database formats/reinitializes the entire SD card and deletes all existing SD contents.**

The UI must state this explicitly. Do not phrase it as if only the old DB directory may be removed.

Do not design a multi-generation rollback system that assumes the old DB survives formatting.

### Required two-phase workflow

```text
Browser selects .nbdb
        ↓
Browser reads/validates locally in bounded slices
        ↓
Device receives/validates header metadata too
        ↓
UI shows explicit "FORMAT ENTIRE SD / DELETE ALL CONTENT" confirmation
        ↓ user confirms
Device unmounts/reinitializes/formats whole SD
        ↓
Browser streams DB payload over HTTP
        ↓
Device writes directly to SD as a .part file
        ↓
CRC/SHA/size/final-open verification
        ↓
rename .part -> final nearby.nbdb
        ↓
DB status = valid/ready
```

### Before formatting SD

The selected local file must be rejected before destructive formatting if any of these fail:

1. at least 128 bytes;
2. exact declared `file_size`;
3. supported magic/format/schema/reader ABI;
4. header CRC;
5. valid monotonic non-overlapping manifest/payload ranges;
6. manifest parse/provenance/source count;
7. release license policy;
8. payload CRC32;
9. payload SHA-256 when required.

The browser may hash the local file in slices; it must **not** read the whole DB into JavaScript memory at once.

The device should repeat the cheap header/compatibility validation before executing the format operation.

### After formatting

Recommended simple layout:

```text
/nearby/
  db/
    nearby.nbdb.part   # while streaming
    nearby.nbdb        # only after successful verification/rename
```

No `current` generation pointer is required for v0.1.

If upload/power fails after formatting, the SD may contain no valid DB. That is an accepted consequence of the explicit destructive workflow; Settings must report DB missing/invalid instead of pretending rollback succeeded.

### Streaming constraints

- never buffer the whole DB in ESP32 RAM;
- never stage the whole DB in internal flash;
- bounded network/write buffer target <=16 KiB;
- count exact received bytes;
- compute/check integrity while streaming and/or final sequential verification;
- flush/close before final validation;
- rename to final filename only after success.

Agent C defines file semantics and validity. Agent D owns actual FATFS/SDSPI/HTTP transport. Agent E owns the browser interaction/warnings.

---

## 15. Home Assistant semantic core (Agent A)

Agent A has a deliberately narrow job:

> **Take already matched and already parsed semantic information and turn it into the smallest faithful RAM-only Home Assistant Device / Entity / State runtime.**

A does **not** identify Xiaomi, SwitchBot, Shelly, Zigbee products, BLE payloads, Wi-Fi frames, or other vendors/protocols.

Conceptual boundary:

```text
already matched + already parsed HA-like semantic fields
        ↓
Agent A
        ↓
Device + Entity + State
        ↓
Agent E
```

### A owns

- audit/cut-down of HA Device Registry semantics;
- audit/cut-down of HA Entity Registry semantics;
- State semantics/store/update;
- Device→Entity ownership;
- one-boot create/update/remove lifecycle;
- cascade deletion;
- existing HA domains/device_class/state/attributes semantics;
- minimal service callback direction;
- stable enumeration/getter APIs for UI;
- generic already-matched fixtures;
- RAM footprint/capacity tests.

### A explicitly does not own

- BLE advertisement parsing;
- Xiaomi or any vendor binary parser;
- Wi-Fi/Zigbee/Matter discovery parsing;
- recognition matcher tables;
- product database;
- vendor-specific production integration modules as a coverage project;
- LVGL rendering;
- radio scheduling.

Integration/Platform concepts may remain as thin HA-compatible lifecycle/ownership semantics, but A must not grow into a second vendor/parser framework.

### Initial domains

Start with existing HA domains required by real devices/UI:

- `sensor`
- `binary_sensor`
- `switch`
- `light`
- `button`

Add existing HA domains only when actually needed, e.g.:

- `number`
- `select`
- `climate`
- `cover`
- `lock`
- `media_player`
- `device_tracker`

Never add speculative NearBy-specific domains.

### Service semantics

Preserve HA names/direction where practical:

- `switch.turn_on`
- `switch.turn_off`
- `light.turn_on`
- `light.turn_off`
- `button.press`

A service call flows to integration/protocol code through a minimal callback; protocol operation then publishes resulting State. During a full scan, the lower hardware path is busy and service execution must not compete with the scan session.

### Runtime reset

```text
BOOT / session reset
  -> Device count = 0
  -> Entity count = 0
  -> State count = 0
```

No restore-state.

---

## 16. Main LVGL UI (Agent E)

Target: **170×320 portrait**.

Visual language: classic Home Assistant-inspired **blue/white**, light background, white rounded cards, dark text, subtle borders, no expensive shadow/blur-heavy UI.

### Fixed top bar

Global main-screen top bar:

```text
[ Scan ]     NearBy One NEXT     [ Settings ]
```

- left: fixed Scan icon button;
- center: small `NearBy One NEXT` title;
- right: fixed Settings gear;
- keep it compact so the list gets most vertical space;
- ~28–34 px bar / ~24–30 px touch-target region is a starting range, not a hard framework;
- tune exact sizes on real hardware.

### Progress bar

- thin horizontal progress directly below the top bar;
- about 3–5 px is the intended visual scale;
- no Windows-XP-style flashlight/magnifier animation;
- no large scanning animation;
- Scan icon may show an active state but should not consume extra layout;
- progress is actual completed scanner modules, not fake timer progress.

### Nearby/Home list

Single-column, vertically scrollable Device cards.

Each Device card contains **only**:

- Device icon
- Device display name

Do **not** show on the Nearby card:

- Entity states;
- controls;
- manufacturer;
- model;
- RSSI;
- protocol;
- transport;
- UUID/MAC/cluster/endpoint;
- custom per-domain summaries;
- technical/debug metadata.

All Devices use one generic card layout regardless of vendor/protocol/domain composition.

Starting card height: roughly **44–50 px** with ~6–8 px gaps, tuned on actual panel.

Rules:

- fixed header;
- list scrolls underneath;
- native LVGL vertical scrolling;
- no pagination;
- no logical cap equal to visible-card count;
- names get most horizontal space; truncate only when necessary.

### Device detail

Tapping a Device when IDLE opens a vertically scrollable detail screen.

Only here enumerate that Device's Entities.

Generic rendering is based on HA domain/state/attributes, not vendor/protocol branches.

Examples:

| HA domain | UI treatment |
|---|---|
| `sensor` | value/readout + unit |
| `binary_sensor` | state/badge |
| `switch` | toggle |
| `light` | on/off; optional brightness/color when supported |
| `button` | action button |
| `number` | slider/input |
| `select` | picker |
| `climate` | compact climate controls if supported |
| `cover` | compact cover controls |
| `lock` | lock/unlock semantics if safely supported |
| `media_player` | compact media controls |

Manufacturer/model may appear in Device detail if useful, but not on Nearby cards.

### UI memory/lifecycle rule

The production UI must not maintain a second permanent Device registry separate from Agent A.

Mock registries are acceptable only during bring-up.

Once A is connected:

- enumerate A's Device/Entity/State data;
- use lightweight state-change notifications;
- delete or reuse transient Detail/Settings screens rather than keeping multiple large LVGL screens alive unnecessarily.

---

## 17. Settings screen

Settings is intentionally tiny. It is not a Home Assistant configuration center.

v0.1 contains only:

```text
Settings
├─ Web Management
├─ Database
│  └─ current DB version/status
└─ Firmware
   └─ current firmware version
```

Do not add unrelated toggles/preferences unless explicitly approved later.

There is no on-device software keyboard for Wi-Fi credentials and no LCD file picker for the DB.

### Web Management entry

Pressing Web Management asks D to start a **temporary** setup SoftAP + local HTTP portal.

LCD should show only connection instructions/status, e.g.:

```text
Web Management

Connect your phone to:
NearBy-One-XXXX

PIN: 12345678        # when provided

Then open:
192.168.4.1

[ Stop Portal ]
```

Temporary password/PIN displayed on the LCD is preferred.

SoftAP is **not continuously enabled**. It runs only after explicit user action and should stop when the setup flow is finished or the user stops it.

A captive-portal auto-open experience is desirable; `192.168.4.1` remains the fallback.

---

## 18. Web Management portal

Mobile-first browser UI. Exactly two primary tabs in v0.1:

```text
NearBy One Setup
[ Wi-Fi ] [ Database ]
```

### Wi-Fi tab

Required UX:

- scan nearby Wi-Fi networks;
- selectable SSID list;
- friendly signal indication;
- manual SSID input fallback;
- password field;
- one clear Connect/Save action;
- progress;
- success/failure status.

Do not expose low-level Wi-Fi driver terminology.

D owns AP/APSTA/STA plumbing, scan and connection transport. E owns browser UX.

**Wi-Fi credentials are session-only in v0.1. Do not silently persist them to NVS/SD.**

### Database tab

Required UX:

- browser file picker;
- current DB metadata/status;
- explicit warning that DB installation **formats the entire SD and deletes all content**;
- validation before destructive confirmation;
- explicit confirmation checkbox/action;
- stages such as:
  - Validating file
  - Preparing / Formatting SD
  - Uploading database
  - Verifying database
  - Installed / Failed
- upload progress;
- final validation result.

Do not invent DB schema in the Web UI. C owns it.

---

## 19. Local networking / provisioning backend (Agent D)

D owns the backend infrastructure required by the Web Portal:

- `esp_netif`/lwIP initialization;
- temporary SoftAP;
- APSTA/STA transition where needed for provisioning;
- Wi-Fi network scan endpoint;
- connect/disconnect lifecycle;
- lightweight `esp_http_server` or equivalent ESP-IDF-native HTTP service;
- static portal asset serving;
- database header/preflight endpoint contract;
- whole-SD format trigger only after validated/confirmed request;
- streaming upload body directly to FATFS/SDSPI;
- status/progress/error reporting;
- clean stop/deinit.

D does not decide recognition DB validity beyond calling/obeying C's contract.

---

## 20. Security and responsible-use scope

The mainline product supports:

- passive observation;
- nearby device/protocol identification;
- packet/discovery analysis required for identification;
- authorized interaction with user-owned devices;
- normal protocol control paths where the user is entitled to control the device.

Do **not** make offensive capabilities a product feature, including:

- Wi-Fi deauthentication;
- WPA/WPA2 credential cracking;
- credential harvesting;
- rogue authentication;
- BLE hijacking;
- Zigbee key extraction;
- TLS MITM;
- poisoning/spoofing attacks intended to capture credentials;
- exploit delivery;
- persistence on third-party devices.

Mature security/scanner projects may be studied for parser/capture techniques, but offensive paths are excluded.

Useful audit/reference projects include:

- hcxdumptool — MIT; strong candidate for direct reuse of safe Wi-Fi parsing pieces where applicable
- BtleJack — MIT
- mitmproxy — MIT, reference only for safe parsing concepts
- Wireshark — GPLv2
- Bettercap — GPLv3
- Scapy — GPLv2
- Aircrack-ng — GPLv2
- Responder — GPLv3
- Kismet — mixed/custom per-file; inspect before use
- nRF Sniffer — inspect exact component licensing

License compatibility must be checked before copying code. Reference analysis is not the same as redistributing source.

---

## 21. Upstream-reuse principle

Before writing a new behavior:

1. search current Home Assistant Core or the relevant protocol project;
2. determine whether the concept, matcher, field, lifecycle, parser, or table already exists;
3. reuse the existing name/semantics directly where practical;
4. copy/adapt permissively licensed code only when that materially reduces custom code;
5. otherwise mechanically generate compact MCU data from upstream;
6. record provenance/license;
7. only then write new MCU glue.

NearBy One NEXT should look like an MCU-sized subset/port of established semantics, not a new ecosystem invented beside them.

---

## 22. Agent ownership and branch workflow

Each Agent works only on its branch and keeps committing there. **Agents do not merge their own PRs.** Integration/merging is a separate coordinated step after review.

### Agent A — HA semantic core

Branch:

```text
agent-a/home-assistant-audit
```

PR #1.

Long-term scope:

- cut down HA Device/Entity/State code/semantics;
- RAM-only runtime;
- generic already-matched semantic input → Device/Entity/State;
- stable API for E;
- footprint/capacity/lifecycle tests.

Must not become a Xiaomi/vendor/protocol/recognition workstream.

### Agent B — scanner/parser/discovery

Branch:

```text
agent-b/scanner-projects-audit
```

PR #2.

Long-term scope:

- HA scanner behavior;
- protocol parsers;
- matcher-input generation;
- cache/dedup/lifecycle;
- complete-scan orchestration/module completion;
- consume D's real native inputs;
- safe upstream parser reuse.

Must not own BSP, recognition DB storage, HA runtime, or UI.

### Agent C — recognition DB

Branch:

```text
agent-c/device-database
```

PR #3.

Long-term scope:

- source/provenance/license matrix;
- real pinned upstream data generation;
- DB format/indexes/reader;
- HA discovery datasets, ZHA, Z2M, assigned numbers, product metadata;
- conflict/coverage/release reports;
- destructive-install validation semantics;
- target lookup benchmark after D hardware support.

Must not write protocol parsers, Device/Entity runtime, HTTP transport, or UI.

### Agent D — hardware/ESP-IDF

Branch:

```text
agent-d/hardware-driver-audit
```

PR #4.

Long-term scope:

- Waveshare BSP;
- ST7789/CST816/SD/shared SPI;
- Wi-Fi/NimBLE/802.15.4 real driver lifecycle;
- native input handoff/lifetime;
- scan-session RF lock;
- SoftAP/HTTP/Wi-Fi provisioning;
- whole-SD streaming upload transport;
- RAM/stack/timing/bench measurements.

Must not write B's protocol parsers or C's DB semantics.

### Agent E — LVGL/Web UX

Branch:

```text
agent-e/lvgl-ha-ui
```

PR #5.

Long-term scope:

- 170×320 LVGL Nearby/Detail/Settings UI;
- HA-style generic Entity rendering;
- scan progress + full touch freeze;
- mobile Web Portal UX;
- integration with A/D/C public contracts.

Must not depend on vendor/protocol internals and must not create a separate semantic/ViewModel layer.

---

## 23. Dependency rule for Agents

An Agent must not stop just because another Agent's real interface or hardware evidence is not ready.

Use the narrowest fixture/stub at the agreed boundary and continue all work that does not require the missing dependency.

Examples:

- A uses generic already-matched semantic fixtures, never raw Xiaomi BLE bytes;
- B uses captured/mock native observations until D's real driver path is available;
- C uses pinned checkout fixtures/host generators until target SD benchmarking is possible;
- E uses Device/Entity/State mocks until A's stable API is ready;
- D writes complete compilable lifecycle paths and marks only true physical measurements `BENCH_REQUIRED`.

`BENCH_REQUIRED` means "code/spec is ready but physical-board evidence is still required". It does **not** mean the Agent is done or should stop all other work.

Never fabricate bench numbers.

---

## 24. Current implementation priorities

The project should converge vertically in this order without violating Agent boundaries.

### D priorities

1. real Wi-Fi start/stop/active scan/promiscuous receive;
2. real NimBLE start/stop/GAP receive;
3. real IEEE 802.15.4 start/stop/channel/receive;
4. robust scan-session error recovery;
5. CST816;
6. SDSPI/FATFS on shared SPI2;
7. SoftAP/HTTP/Wi-Fi provisioning;
8. whole-SD streaming DB upload;
9. physical RAM/stack/timing/coexistence tests.

### B priorities

1. consume D native contract;
2. HA Bluetooth matcher/caching lifecycle;
3. Zeroconf;
4. SSDP;
5. DHCP;
6. Wi-Fi management discovery parsing;
7. IEEE 802.15.4/Zigbee/Matter discovery parsing as needed;
8. complete-scan orchestration and robust module terminal events;
9. parser test vectors and upstream reuse audit.

### C priorities

1. keep whole-SD destructive model consistent everywhere;
2. build from real pinned HA Core + ZHA sources and record coverage report;
3. expand HA Zeroconf/SSDP/DHCP datasets;
4. expand Zigbee2MQTT;
5. add assigned-number/vendor sources subject to licensing;
6. improve protocol-specific indexes/conflict reports;
7. scale tests 100k/500k/1M records;
8. target ESP32-C6 lookup/SD benchmark with D.

### A priorities

1. reduce/default RAM footprint;
2. capacity/error/reset/lifecycle tests;
3. preserve only the minimal HA Device/Entity/State semantics;
4. generic already-matched semantic-input path;
5. stable E-facing enumeration/state/service API;
6. no vendor/protocol-specific expansion.

### E priorities

1. keep boot idle/empty;
2. fresh result generation per new scan;
3. compact Home/Detail/Settings implementation;
4. scan-time top-layer touch blocker;
5. destroy/reuse transient screens to protect RAM;
6. connect A's real registries instead of keeping a second permanent UI registry;
7. connect D/C Web backend contracts;
8. hardware touch/layout tuning.

---

## 25. Testing requirements by layer

### Agent A host tests

At minimum:

- boot/reset empty;
- Device create/update/remove;
- identifier/connection lookup;
- Entity identity uniqueness;
- missing Device reference rejection;
- Device→Entity attachment;
- State create/update/remove;
- attributes;
- service callback support;
- disabled/unavailable Entity behavior;
- cascade deletion;
- repeated resets;
- pool exhaustion;
- oversized/invalid inputs;
- `sizeof()` and total pool report.

### Agent B host tests

- representative BLE AD structures;
- malformed/truncated BLE;
- Wi-Fi frame vectors;
- mDNS/Zeroconf matcher vectors;
- SSDP lifecycle vectors;
- DHCP hostname/OUI matching;
- Zigbee/802.15.4 vectors as added;
- dedup/update/remove behavior;
- scan-module completion/error paths;
- no offensive feature paths.

### Agent C host tests

- generator determinism from pinned inputs;
- header validation;
- corruption detection;
- release license gate;
- lookup hit/miss;
- malformed ranges/offset overflow;
- schema/ABI rejection;
- large database scaling;
- conflict/coverage reports;
- whole-SD install contract simulation;
- real upstream build evidence, not only synthetic fixtures.

### Agent D build/bench tests

Static/build:

- ESP-IDF build for ESP32-C6;
- driver lifecycle compile paths;
- scan-session contention/error cleanup;
- bounded queue/ring behavior;
- no callback/ISR per-packet malloc.

Physical `BENCH_REQUIRED`:

- LCD four corners/color/inversion;
- CST816 four corners;
- SD mount/format/read/write;
- LCD+SD simultaneous/shared-bus behavior;
- Wi-Fi active/promiscuous real receive;
- NimBLE real advertising receive;
- 802.15.4 real receive;
- RF phase switching/recovery;
- free heap before/after init/deinit;
- stack high-water marks;
- start/stop/deinit latency;
- first-RX latency after switching;
- drop counters under load;
- SoftAP/HTTP streaming behavior.

### Agent E tests

- exact 170×320 layout;
- no automatic scan;
- boot empty state;
- scrollable single-column cards;
- card content only icon+name;
- Device detail generic domains;
- full scan-time input lock while repainting continues;
- progress driven only by module completion;
- no Settings/detail/service during scan;
- transient screen memory lifecycle;
- Web Portal mobile layout;
- explicit whole-SD destructive warning;
- no full-file JS buffering;
- final touch sizing/legibility on physical board.

---

## 26. End-to-end v0.1 acceptance flow

A healthy v0.1 should support this complete user story:

```text
Power on
  ↓
Nearby screen is IDLE and empty
  ↓
User taps Scan
  ↓
UI freezes touch, keeps rendering
  ↓
D acquires full scan-session RF gate
  ↓
B runs every enabled scanner module exactly once
  ↓
D supplies native observations
  ↓
B parses/deduplicates and derives matcher keys
  ↓
C performs bounded read-only recognition lookups
  ↓
A receives already-matched/already-parsed HA-like semantic information
  ↓
A creates/updates RAM-only Device/Entity/State
  ↓
E inserts Device icon+name cards as results appear
  ↓
B reports every module terminal completion
  ↓
progress reaches 100%
  ↓
D/B release scan session
  ↓
E removes touch blocker and returns to IDLE
  ↓
User opens a Device
  ↓
E enumerates that Device's HA Entities from A
  ↓
User invokes an allowed Entity service
  ↓
service goes through the integration/protocol path only while RF path is available
  ↓
resulting State updates and UI refreshes
```

Web setup story:

```text
Settings
  ↓
Web Management
  ↓
Temporary SoftAP starts
  ↓
Phone opens portal
  ├─ Wi-Fi: scan/select/password/connect for this boot session
  └─ Database:
       select local .nbdb
       → local pre-validation
       → explicit ENTIRE-SD format confirmation
       → device formats SD
       → browser streams DB directly to SD
       → CRC/SHA/size verification
       → final rename
       → Settings shows DB version/status
```

---

## 27. Definition of project direction

When evaluating a new feature or patch, ask:

1. Does Home Assistant already define the semantic concept?
2. Does an upstream parser/scanner already implement the protocol behavior?
3. Is this static knowledge that belongs in the recognition DB instead of firmware logic?
4. Is this MCU glue that genuinely must be new?
5. Does it respect the A/B/C/D/E boundary?
6. Does it avoid runtime persistence?
7. Does it fit the no-PSRAM memory budget?
8. Does it preserve full-scan RF exclusivity?
9. Does the normal UI remain protocol/vendor-agnostic?
10. Is any physical claim properly marked `BENCH_REQUIRED` until measured?

If a proposal creates a new abstraction layer, persistent registry, vendor-specific UI, dynamic parser VM, or duplicated Home Assistant concept, the default answer is **no** unless there is a demonstrated requirement that cannot be satisfied by the established architecture.

---

## 28. Short invariant summary

```text
Target board: Waveshare ESP32-C6-Touch-LCD-1.9
Framework: ESP-IDF
Display: 170x320 portrait, ST7789V2 baseline, CST816 touch
PSRAM: none assumed
Runtime persistence: none
Persistent product data: recognition DB on SD only
Boot scan: no
Scan: user-triggered complete pass across all modules
Scan progress: real module completion, not timer
Scan interaction: full touch lock, rendering continues
RF concurrency: full scan owns exclusive lower-level gate
Home card: icon + Device name only
Detail: generic HA Entity rendering
Settings: Web Management + DB status/version + firmware version only
Web Portal: Wi-Fi + Database only
DB install: validate first, then FORMAT ENTIRE SD, then bounded streaming upload
Semantic model: Home Assistant Device / Entity / State
Capability layer: forbidden
Cross-protocol identity fusion: not v0.1
Offensive wireless features: excluded
A: HA semantic core only
B: scanners/parsers/discovery
C: recognition DB/knowledge
D: hardware/drivers/network/SD transport
E: LVGL/Web UX
Agents work on their own branches and do not merge their own PRs
```
