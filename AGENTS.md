# NearBy One NEXT — Refactor Development Rules

## Canonical source of truth

On branch `refactor/native-kismet-baseline`, the root `README.md` is the canonical architecture and development baseline.

This `AGENTS.md` is the execution contract for agents performing the refactor.

If older integration notes, frozen-agent documents, historical scanner architecture, PR descriptions, or legacy implementation details conflict with this branch's root README, the root README wins unless the user explicitly decides otherwise.

---

## Mission

Refactor NearBy One NEXT from a single integrated scanner firmware into an application-oriented ESP32-C6 handheld platform with directly usable native APIs plus project-organized Level-2 APIs:

```text
Application
├──────────────> ESP-IDF / FreeRTOS / NimBLE / lwIP / LVGL native APIs
├──────────────> kismet_*      acquisition / scanning / observation
├──────────────> wireshark_*   bounded protocol parsing / dissection
└──────────────> ha_*          recognition / Device-Entity-State semantics
```

The target board remains **Waveshare ESP32-C6-Touch-LCD-1.9**.

---

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
15. Prefer mechanical migration first; do not combine namespace moves with unrelated algorithm rewrites unless required to make the phase buildable or correct.

---

## Public API roles

Use these defaults when deciding ownership:

```text
kismet_*       acquire observations
wireshark_*    parse/dissect protocol data
ha_*           recognize/model device semantics
native APIs    direct low-level control
application    product workflow/UI/policy
```

The decision rule is:

```text
What does this function primarily do?

acquire / scan / schedule observations
    -> kismet_*

parse protocol fields from bytes
    -> wireshark_*

recognize a device / model device semantics
    -> ha_*

perform one low-level hardware/platform operation
    -> native ESP-IDF / NimBLE / lwIP / FreeRTOS / LVGL or narrow BSP

implement one product flow, screen, progress policy, module order, or button behavior
    -> application
```

Do not force an API into a project namespace when the implementation does not actually provide that project's distinct reusable capability.

---

## Kismet boundary

Kismet owns finite acquisition workflows such as Wi-Fi active/passive scans, BLE scans, IEEE 802.15.4 scans, LAN discovery scans, bounded deduplication, observation statistics, and useful scan mechanics.

It does not own trivial aliases for native lifecycle functions.

Good:

```c
kismet_wifi_scan_active(...);
kismet_wifi_scan_passive(...);
kismet_ble_scan(...);
kismet_i154_scan(...);
kismet_mdns_discover(...);
kismet_ssdp_discover(...);
kismet_dhcp_observe(...);
```

Bad:

```c
kismet_wifi_stop(); /* if it only forwards to esp_wifi_stop() */
```

Whole-product scan ordering, enabled modules, progress UI, touch locking, and fixed “scan all” policy belong to the application unless they later become independently reusable across Apps.

---

## Wireshark boundary

Wireshark owns bounded dissector-style parsing helpers migrated from the existing parser code, including where applicable:

```c
wireshark_80211_parse_mgmt(...);
wireshark_ble_parse_adv(...);
wireshark_ble_find_manufacturer_data(...);
wireshark_ble_find_service_data(...);
wireshark_ble_has_service_uuid(...);
wireshark_mdns_parse(...);
wireshark_ssdp_parse(...);
wireshark_dhcp_parse(...);
wireshark_i154_parse_mac(...);
wireshark_zigbee_parse(...);
```

Preserve fixed storage limits, malformed/partial handling, truncation markers, and native metadata needed by applications.

Do not claim a protocol parser exists under a Wireshark name when the firmware currently implements only a higher-level discovery semantic subset.

---

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

Matter discovery semantic extraction currently belongs here rather than under Wireshark:

```c
ha_matter_from_ble(...);
ha_matter_from_mdns(...);
```

Existing `ha_device_*`, `ha_entity_*`, `ha_state_*`, and service APIs should be retained/stabilized where useful.

HA semantics are optional per App. A Wi-Fi Analyzer, BLE Explorer, or Packet Inspector must not be forced through HA Device/Entity/State.

---

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

Resource coordination should normally stay internal to Kismet/platform code. Do not expose a large public resource-management framework to Apps unless a concrete use case requires it.

---

# Phase execution protocol

This refactor MUST be performed **one phase at a time**.

When an agent is assigned a phase:

1. Read the root `README.md` and this `AGENTS.md` before editing code.
2. Inspect the current branch state first; earlier phases may already have changed names, paths, or compatibility shims.
3. Perform only the assigned phase and the minimum prerequisite fixes needed to keep that phase correct and buildable.
4. Do **not** start work belonging to a later phase.
5. Keep useful behavior and existing test coverage unless the phase explicitly replaces it.
6. Add/update tests for the migrated public contract.
7. Run the relevant host tests and ESP-IDF build/CI path available to the agent.
8. Record any build/test limitation explicitly; do not claim verification that was not performed.
9. Commit the phase in small coherent commits where practical.
10. **STOP after the assigned phase.** Report what changed, tests/build results, remaining compatibility code, and blockers. Wait for an explicit user instruction before beginning the next phase.

An agent MUST NOT decide on its own to “finish the whole refactor” after completing its assigned phase.

If a later-phase change appears attractive while working on an earlier phase, leave a note/TODO or report it to the user instead of implementing it prematurely.

---

# Phase 1 — Public API skeleton and ownership contracts

## Goal

Establish the new component/header structure and type/API ownership without changing product behavior.

## Expected direction

Create or prepare component structure similar to:

```text
firmware/components/
├─ kismet/
│  └─ include/
│     ├─ kismet_wifi.h
│     ├─ kismet_ble.h
│     ├─ kismet_i154.h
│     ├─ kismet_lan.h
│     └─ kismet_dedup.h
│
├─ wireshark/
│  └─ include/
│     ├─ wireshark_80211.h
│     ├─ wireshark_ble.h
│     ├─ wireshark_mdns.h
│     ├─ wireshark_ssdp.h
│     ├─ wireshark_dhcp.h
│     ├─ wireshark_i154.h
│     └─ wireshark_zigbee.h
│
├─ home_assistant/
│  └─ include/
│     ├─ ha_discovery.h
│     ├─ ha_recognition.h
│     └─ ha_core.h
│
└─ internal/
   ├─ radio/
   ├─ packet/
   └─ common/
```

The exact number of files may be simplified when that makes the ESP-IDF component layout cleaner, but ownership boundaries must remain clear.

## Phase 1 work

- Define target public headers and target names/types.
- Establish CMake component dependencies.
- Add compatibility typedefs/wrappers only when necessary to keep the existing tree buildable.
- Prefer no runtime behavior change.
- Do not delete legacy public APIs yet.
- Do not move radio lifecycle implementation yet.
- Do not rebuild the Scanner app yet.

Representative ownership decisions:

```text
nearby_wifi_mgmt_facts_t           -> wireshark 802.11 type
nearby_ble_discovery_facts_t       -> wireshark BLE type
nearby_i154_mac_facts_t            -> wireshark 802.15.4 type
nearby_zigbee_discovery_facts_t    -> wireshark Zigbee type
nearby_wifi_passive_scan_config_t  -> kismet Wi-Fi scan type
nearby_ble_scan_config_t           -> kismet BLE scan type
nearby_i154_scan_config_t          -> kismet 802.15.4 scan type
```

## Phase 1 exit criteria

- New API component/header skeleton exists.
- Ownership of parser, acquisition, recognition, and App policy is explicit.
- Existing firmware remains buildable or any unresolved build blocker is clearly documented.
- No intentional scanner behavior change.
- Legacy APIs still exist as needed for compatibility.
- Agent stops here.

---

# Phase 2 — Split `discovery_parser` into Wireshark parsing and HA semantics

## Goal

Move the existing parser algorithms into their target project API families while preserving parser behavior.

This phase should be mostly a **mechanical namespace/component migration**, not a protocol rewrite.

## Phase 2A — Wireshark parser migration

Migrate existing parser implementation such as:

```text
wifi_management_parser.c
ble_ad_parser.c
mdns_parser.c
ssdp_parser.c
dhcp_parser.c
i154_mac_parser.c
zigbee_discovery.c
```

Target public APIs include:

```c
wireshark_80211_parse_mgmt(...);
wireshark_ble_parse_adv(...);
wireshark_ble_find_manufacturer_data(...);
wireshark_ble_find_service_data(...);
wireshark_ble_has_service_uuid(...);
wireshark_ble_uuid_format(...);
wireshark_mdns_parse(...);
wireshark_ssdp_parse(...);
wireshark_dhcp_parse(...);
wireshark_i154_parse_mac(...);
wireshark_zigbee_parse(...);
```

Preserve existing bounded storage and parse outcomes such as OK/PARTIAL/MALFORMED/UNSUPPORTED/SECURED where applicable.

## Phase 2B — HA discovery/recognition adapters

Current Matter helpers are semantic discovery helpers rather than full packet dissectors. Move/rename them toward:

```c
ha_matter_from_ble(...);
ha_matter_from_mdns(...);
```

Move existing matcher-key/recognition adapter responsibility toward HA-facing APIs:

```c
ha_match_ble(...);
ha_match_zeroconf(...);
ha_match_ssdp(...);
ha_match_dhcp(...);
```

Add `ha_match_zigbee()` / `ha_match_matter()` only when the existing recognition DB/key capabilities make this a small, truthful extension. Do not invent unsupported recognition behavior merely to complete the list.

The `.nbdb` implementation and low-level DB container API may remain internally named as they are for now.

## Tests

Migrate/update parser tests such as:

```text
test_wifi_discovery.c
test_ble_discovery.c
test_lan_discovery.c
test_i154_zigbee.c
test_matter_discovery.c
test_parser_fuzz.c
```

Keep regression vectors equivalent unless an existing bug is intentionally fixed and documented.

## Phase 2 forbidden work

- Do not move scan runtimes into Kismet yet beyond compatibility glue strictly required by parser rename changes.
- Do not redesign radio ownership.
- Do not rewrite the full Scanner app.
- Do not delete `radio_runtime`.
- Do not remove all legacy parser names until current callers have a safe migration path.

## Phase 2 exit criteria

- Wi-Fi/BLE/LAN/802.15.4/Zigbee parser functionality is available under `wireshark_*` APIs.
- HA semantic/recognition adapters are separated from byte-level dissectors.
- Relevant host parser tests pass.
- Existing firmware callers still build through migrated calls or temporary compatibility shims.
- Agent stops here.

---

# Phase 3 — Move acquisition runtimes into Kismet

## Goal

Turn the existing finite scan runtimes into convenient Kismet acquisition APIs while using Wireshark parsers underneath.

## Source areas

Primary migration input:

```text
wifi_scan_runtime.c
ble_scan_runtime.c
i154_scan_runtime.c
lan_scan_runtime.c
transport_dedup.c
native_handoff (internal support, not necessarily public)
```

## Target APIs

Wi-Fi:

```c
kismet_wifi_scan_active(...);
kismet_wifi_scan_passive(...);
```

BLE:

```c
kismet_ble_scan(...);
```

802.15.4:

```c
kismet_i154_scan(...);
```

LAN discovery:

```c
kismet_mdns_discover(...);
kismet_ssdp_discover(...);
kismet_dhcp_observe(...);
```

Deduplication:

```c
kismet_dedup_init(...);
kismet_dedup_reset(...);
/* protocol-specific helpers where useful */
```

Exact names may be adjusted for C clarity, but remain in the correct project namespace.

## Required dependency direction

Examples:

```text
native Wi-Fi callback / handoff
    -> kismet acquisition
    -> wireshark_80211_parse_mgmt()
    -> optional dedup
    -> App callback

NimBLE GAP callback / handoff
    -> kismet_ble_scan()
    -> wireshark_ble_parse_adv()
    -> optional dedup
    -> App callback

ESP-IDF 802.15.4 RX
    -> kismet_i154_scan()
    -> wireshark_i154_parse_mac()
    -> optional wireshark_zigbee_parse()
    -> App callback
```

Keep `native_handoff` or equivalent bounded callback-safe storage internal unless there is a concrete reason for Apps to consume it directly.

## Phase 3 forbidden work

- Do not rebuild fixed all-module Scanner policy as `kismet_scan_all()`.
- Do not move App progress/touch/UI policy into Kismet.
- Do not delete `radio_runtime` wholesale yet.
- Do not perform broad BSP/LVGL redesign.

## Phase 3 exit criteria

- Existing finite Wi-Fi/BLE/LAN/802.15.4 acquisition behavior is exposed through `kismet_*` APIs.
- Kismet calls Wireshark for parser work rather than owning duplicate parser implementations.
- Existing scan statistics/drop accounting remain available where useful.
- Relevant scan/runtime tests pass.
- Agent stops here.

---

# Phase 4 — Resource/lifecycle cleanup and native coexistence

## Goal

Make Kismet workflows coexist safely with Apps that call ESP-IDF/NimBLE APIs directly, and remove redundant wrapper behavior from the old radio runtime without destabilizing the firmware.

## Source areas

Inspect carefully:

```text
radio_runtime/
  wifi_runtime.c
  nimble_runtime.c
  i154_runtime.c
  radio_phase.c
  radio_cleanup.c

scan_session/
native_handoff/
```

## Required strategy

Do **not** begin by deleting `radio_runtime`.

Use this order:

```text
migrate callers to new Kismet/native paths
-> identify trivial wrappers that only rename native APIs
-> replace those callers with native ESP-IDF/NimBLE calls
-> identify wrappers that contain real lifecycle/recovery/resource logic
-> move that real logic into Kismet/internal resource support
-> remove dead wrapper functions only after all callers are migrated
```

Resource workflow should generally be:

```text
inspect current state
-> acquire minimum lease
-> configure/start required subsystem
-> perform finite operation
-> teardown operation
-> restore previous state when practical
-> release lease
```

Keep fatal cleanup/recovery behavior if it protects the hardware from inconsistent radio state, but simplify obsolete “whole product scan owns everything” assumptions.

## Public API rule

Prefer resource coordination to remain internal. Do not create a public general-purpose NearBy HAL/resource framework.

## Phase 4 forbidden work

- Do not redesign application UI.
- Do not introduce fixed Scanner module order into Kismet.
- Do not remove compatibility wrappers that still have live callers.
- Do not replace useful recovery logic merely for naming purity.

## Phase 4 exit criteria

- Kismet acquisition can safely borrow/restore relevant radio state under the supported use cases.
- Trivial `nearby_*` wrappers around native APIs are removed or have a documented remaining reason.
- Real lifecycle/recovery logic has a clear internal/Kismet home.
- No dead live-call references to removed `radio_runtime` APIs.
- Firmware build/test path passes or blockers are explicitly documented.
- Agent stops here.

---

# Phase 5 — Rebuild the Scanner as an application

## Goal

Move whole-product scan composition out of platform components and into the Scanner application.

## Main source areas

```text
scan_coordinator/
nearby_scan_pipeline.*
nearby_semantic_apply.*
main.c
existing UI scan flow
```

## Target behavior

The Scanner app may compose project APIs directly, for example conceptually:

```c
kismet_wifi_scan_active(...);
kismet_wifi_scan_passive(...);
kismet_mdns_discover(...);
kismet_ssdp_discover(...);
kismet_dhcp_observe(...);
kismet_ble_scan(...);
kismet_i154_scan(...);
```

The App decides:

- which modules are enabled;
- module order;
- per-module duration/settings;
- cancellation policy;
- progress calculation;
- touch/button behavior;
- whether observations are passed to `ha_match_*`;
- whether recognized devices are inserted into `ha_device_*` / `ha_entity_*` / `ha_state_*`;
- LVGL presentation.

Do not put these product decisions back into Kismet simply to preserve the old `scan_coordinator` shape.

`nearby_semantic_apply` is App glue unless a clearly reusable semantic adapter emerges.

## Phase 5 exit criteria

- Scanner product behavior is implemented as an App-level composition of native/Kismet/Wireshark/HA APIs.
- Platform APIs no longer require a fixed seven-module all-scan policy.
- UI/progress/module-order logic lives in the App.
- Existing useful scanner behavior is preserved or intentional changes are documented.
- Agent stops here.

---

# Phase 6 — Legacy cleanup and final boundary enforcement

## Goal

Remove migration shims and obsolete component boundaries only after the new architecture is proven by real App usage.

## Recommended cleanup order

```text
1. legacy recognition adapter names/shims
2. legacy nearby_* parser public headers
3. legacy nearby_* scan public headers
4. obsolete scan_coordinator implementation
5. obsolete scan_session assumptions/shims
6. dead radio_runtime wrappers/component pieces
7. obsolete main/ pipeline glue
```

Do not mechanically delete a component because its old name is undesirable. Delete only when all useful behavior has a new owner and all callers are migrated.

## Final checks

Search the repository for:

```text
nearby_wifi_parse_
nearby_ble_parse_
nearby_*_scan_run
nearby_scan_coordinator
scan_session_
nearby_wifi_driver_
nearby_nimble_
nearby_i154_
```

Classify every remaining occurrence as one of:

```text
intentional internal compatibility
legacy test fixture
historical documentation
must migrate/remove
```

Also verify dependency direction:

```text
App -> Native
App -> Kismet -> Native + Wireshark
App -> Wireshark
App -> HA
HA recognition -> DB implementation
BSP -> Native
```

Native/BSP must not depend on Kismet/Wireshark/HA/App.

## Phase 6 exit criteria

- Obsolete public `nearby_*` umbrella APIs are removed except intentionally retained internal names or historical fixtures.
- No duplicate parser/scanner implementations exist solely for namespace compatibility.
- New Apps use the intended project API families or native APIs directly.
- Repository docs match actual source layout.
- Tests and ESP-IDF build pass to the extent available.
- Agent stops and reports final migration status.

---

## Suggested commit granularity

Prefer small coherent commits. A typical progression may look like:

```text
Phase 1
  add project API component skeletons

Phase 2
  move Wi-Fi parser to wireshark namespace
  move BLE parser to wireshark namespace
  move LAN parsers to wireshark namespace
  move 802.15.4/Zigbee parsers
  move HA discovery/recognition adapters

Phase 3
  move Wi-Fi scan runtime into Kismet
  move BLE scan runtime into Kismet
  move LAN/802.15.4 runtimes into Kismet
  move bounded dedup into Kismet

Phase 4
  extract shared radio/resource lifecycle support
  migrate native wrapper callers
  remove dead radio wrappers

Phase 5
  rebuild Scanner app on new APIs

Phase 6
  remove compatibility APIs and obsolete components
```

These are suggested commit boundaries, not permission to cross phase boundaries.

---

## Migration direction summary

Current code should move roughly as follows:

```text
nearby_board          -> narrow BSP
nearby_lvgl_port      -> thin lvgl_port
radio_runtime         -> native calls + internal resource/lifecycle support
native_handoff        -> internal bounded callback handoff
scan_coordinator      -> App policy after reusable mechanics are extracted
scan_session          -> internal resource/Kismet support
discovery_parser      -> wireshark_* parsers + kismet_* acquisition + ha_* semantic adapters
recognition_db        -> bounded recognition implementation behind ha_* entry points where appropriate
ha_core               -> stable ha_* semantic APIs
nearby_scan_pipeline  -> Scanner App composition
nearby_semantic_apply -> App glue
main.c                -> boot + platform/app runtime startup only
```

---

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

---

## LVGL rule

LVGL is exposed directly to applications.

A thin port may initialize display/touch integration and enforce the LVGL owner-task rule, but do not build wrapper APIs that mirror ordinary `lv_*` widgets/functions.

---

## FreeRTOS rule

Applications and Level-2 modules may directly use FreeRTOS tasks, queues, mutexes, event groups, and timers.

Do not create a second project-specific RTOS abstraction solely for naming consistency.

---

## Provenance and licensing

When behavior is materially copied, translated, adapted, generated, or derived from an upstream project, preserve the relevant repository, immutable revision/tag, source path, license, and provenance classification.

A public namespace is not a substitute for legal/source provenance documentation.

Reference-only sources must remain reference-only unless project distribution policy explicitly permits incorporation.

Do not create a public Bettercap/Aircrack-ng/hcxdumptool/BtleJack/nRF Sniffer/Responder/mitmproxy API merely to acknowledge inspiration. Create such a namespace only when a distinct reusable runtime capability actually exists and the user approves its addition.

---

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

---

## Security boundary

Passive discovery, protocol parsing, diagnostics, packet observation/capture where appropriate, and authorized device interaction are within scope.

Do not add credential theft, unauthorized access, destructive RF interference, persistence on third-party devices, or access-control bypass features.

Researching or referencing a security project does not imply that all of its offensive capabilities belong in NearBy One NEXT.

---

## Working principle

When deciding where code belongs, preserve this rule:

> **Native APIs provide primitives. Kismet acquires. Wireshark dissects. Home Assistant understands. Apps compose product experiences.**

And when executing the refactor, preserve this rule:

> **One assigned phase at a time. Complete it, verify it, report it, stop. Do not begin the next phase without explicit user instruction.**
