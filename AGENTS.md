# NearBy One NEXT — Integration Agent

## Canonical source of truth

Before doing any work, read the repository root `README.md` on `main`. It is the canonical NearBy One NEXT product specification.

This file defines the **final integration/build role**. It must not broaden, replace, or contradict the root README. If any frozen branch note, old research document, PR description, fixture, or historical implementation conflicts with the root README, the root README wins.

## Mission

Integrate the five frozen agent workstreams into one production ESP-IDF firmware for the Waveshare ESP32-C6-Touch-LCD-1.9 and produce a firmware build that can be flashed to the board.

Do **not** redesign the architecture. The integration path is fixed:

```text
D native hardware/radio/network input
        ↓
B scan orchestration + protocol parsing + normalization
        ↓
C recognition DB lookup / product matching
        ↓
already matched + already parsed semantic result
        ↓
A Home Assistant Device / Entity / State runtime core
        ↓
E LVGL + Web UI
```

## Frozen baselines

Use these frozen branch heads as the integration sources unless the user explicitly provides newer reviewed frozen commits:

- Agent A — `agent-a/home-assistant-audit`
  - frozen commit: `df95156ed1733c766d539d30cbc3c11c10916cb7`
  - owns: RAM-only HA Device / Entity / State core and stable API
- Agent B — `agent-b/scanner-projects-audit`
  - frozen commit: `682d5e74c6d2016641c3179fa5404d0249598faf`
  - owns: scanner orchestration, BLE/Wi-Fi/LAN/802.15.4/Zigbee/Matter parsing, normalization, B→C adapters
- Agent C — `agent-c/device-database`
  - frozen commit: `1fe26d3bc9b0e468534116e86dfa0964aae2ae1f`
  - owns: SD-backed recognition DB, bounded runtime matcher API, generator/provenance
- Agent D — `agent-d/hardware-driver-audit`
  - frozen commit: `accdf91441d0abaf6a5d69aac31394465ab49b06`
  - owns: board/runtime drivers, radio handoff, scan gate, LCD/touch/SD, SoftAP/HTTP, whole-SD DB transport
- Agent E — `agent-e/lvgl-ha-ui`
  - frozen commit: `02627d76fe543c6e13f2944485866ea7a6356592`
  - owns: 170x320 LVGL UX and static Web Portal UX baseline

Do not merge the five agent PRs blindly. Integrate their reviewed source into a dedicated integration branch, resolving conflicts against `main` and this canonical architecture.

## Target

- Board: Waveshare ESP32-C6-Touch-LCD-1.9
- Framework: ESP-IDF v6.1
- Target: `esp32c6`
- Display: 170x320 portrait, ST7789V2 baseline
- Touch: CST816
- No PSRAM assumed
- Runtime state: RAM-only
- Persistent product data: recognition DB on SD only

## Non-negotiable product behavior

- Boot into an empty idle home screen. Do not auto-scan.
- User `Scan` starts one complete pass across every enabled scanner module.
- Scan progress is based on real module terminal completion, not a timer.
- Rendering continues during scan, but all touch input is blocked until the scan session completes/fails and the D scan gate is released.
- Fresh scan generation replaces stale current-session cards.
- Home cards show only icon + Device name.
- Detail UI renders generic HA Entity/state/attributes.
- Settings contains only Web Management, DB status/version, and firmware version.
- Temporary SoftAP is started only from explicit Web Management action.
- Web portal has exactly Wi-Fi and Database tabs.
- Wi-Fi provisioning is session-only and must not persist credentials.
- Database install must prevalidate first, require explicit destructive confirmation, format the entire SD, stream to `/nearby/db/nearby.nbdb.part`, verify, then rename to `/nearby/db/nearby.nbdb`.
- Failure after format may leave no usable DB.

## Architecture boundaries

Do not introduce a new product abstraction layer such as Capability, Action, ViewModel, Provider, Adapter framework, DeviceGraph, generic Observation object, universal Packet framework, plugin VM, dynamic parser language, JavaScript/Python runtime, or general rule engine.

Use Home Assistant semantics directly:

- Device groups a physical device.
- Entity is the capability/state/control unit.
- State is effectively `entity_id + state + attributes`.
- Preserve HA service names where practical.

Cross-protocol physical-device identity fusion is out of scope for v0.1 unless upstream/HA identity provides a reliable merge key.

## Integration order

Work in this order and keep the tree buildable after each stage:

1. Start from current `main` and reread the root README.
2. Create/use a dedicated integration branch. Do not do large integration development directly on `main`.
3. Integrate D first as the hardware/runtime foundation.
4. Integrate C recognition DB component and bounded typed matcher API.
5. Integrate B parser/scanner/coordinator on top of frozen D/C contracts.
6. Integrate A HA Device/Entity/State core.
7. Integrate E UI last.
8. Replace E production mock/private registry with A registry/API. Mock fixtures may remain test-only but must not become a second production registry.
9. Wire the real end-to-end scan path:

```text
E Scan action
→ B scan coordinator
→ D exclusive scan session
→ D native records
→ B parse / normalize / scan-local dedup
→ C recognition lookup
→ B optional compiled parser only when required
→ already matched + parsed semantic result
→ application queue / A owner task
→ A Device / Entity / State
→ E refreshes cards and progress
```

10. Wire Web Management to D session-only Wi-Fi and whole-SD DB upload flow; keep E's existing two-tab UX.
11. Remove or isolate all production stubs that fake scan results, DB status, Wi-Fi status, Device/Entity data, or upload completion.

## A owner-task rule

`ha_core` is not a cross-task shared registry.

- All Device/Entity/State mutations and UI enumeration occur on one owner task.
- B/C/D worker contexts must copy semantic results into a bounded application queue for that owner task.
- `ha_core_revision()` is invalidation detection only, never synchronization.
- Do not solve integration by adding a lock/manager/framework around A unless a reviewed blocker proves unavoidable.

## Recognition rules

- B owns parsing and normalized facts.
- C owns product recognition and candidate matching.
- Do not copy C matcher tables into B.
- `AMBIGUOUS` is fail-closed: do not choose a winner.
- Exact protocol keys such as ZHA/Z2M/Matter may use C exact-key lookup when appropriate.
- HA-derived BLE/Zeroconf/SSDP/DHCP observations must use C typed matcher APIs.
- If C metadata returns a `parser_id`, dispatch only to a statically compiled B parser explicitly supported by firmware. No dynamic execution.

## UI integration rules

The E mock/UI phase is a visual/interaction baseline, not the final production data model.

- Preserve 170x320 portrait layout and HA blue/white card language.
- Top bar remains `[ Scan ] NearBy One NEXT [ Settings ]`.
- During scanning, touch must be fully blocked while display updates continue.
- Do not keep E's private mock Device registry as production state once A is integrated.
- Detail/settings/transient screens should remain destroy/reuse rather than permanent duplicate registries/screens.

## Build and test requirements

Software integration is not complete until all of the following are green on the integrated head:

- ESP-IDF v6.1 default build for `esp32c6`.
- ESP-IDF v6.1 build with all current BENCH_REQUIRED compile paths enabled.
- A host tests including footprint/boundary/owner-task contract.
- B parser vectors, malformed/fuzz tests, scan coordinator tests, and B→C typed recognition contract tests.
- C runtime reader/matcher host tests and pinned DB reproducibility/preflight tests.
- Any E host/static tests that remain applicable after replacing mocks.
- Integration tests for boot-idle, scan lifecycle, fatal cleanup, progress, queue→A owner task, and UI touch-lock state transitions.

Treat warnings as errors where the existing agent CI does so. Do not silence real errors with broad warning suppression.

## Flashable firmware deliverable

The primary deliverable is a normal ESP-IDF flashable build for this board.

A successful final build must leave the standard ESP-IDF artifacts, including at least:

- bootloader binary
- partition table binary
- application firmware binary
- `flash_args` / equivalent ESP-IDF flashing metadata

Provide/document the exact `idf.py` commands used to build and flash, for example:

```text
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```

Do not invent a custom image packer unless required by the board. Prefer standard ESP-IDF output and flashing flow.

Add a release/CI artifact step so a successful integration build uploads the flashable binaries and relevant flashing metadata for download. The artifact must be generated from the same integrated commit that passed CI.

## BENCH_REQUIRED / physical validation

Software completion and physical-board validation are separate.

Do not block creation of the first flashable firmware on measurements that genuinely require the board, but clearly retain them as `BENCH_REQUIRED`, including at minimum:

- real Waveshare ESP32-C6 boot and LCD/touch validation
- RF scan behavior and cleanup on hardware
- RAM/heap measurements on ESP32-C6
- SD recognition lookup latency on real microSD
- whole-SD destructive import on real media
- SoftAP/Web portal behavior on phone
- full end-to-end scan → recognition → A → E flow

Never fabricate board measurements. Record unperformed physical checks explicitly.

## Memory and persistence

No PSRAM is assumed. Keep all queues, rings, parser buffers, lookup workspaces and UI/runtime pools bounded.

Do not persist runtime product state, including Devices/Entities/States, scan results, favorites, rooms, dashboards, history, automations, runtime identity maps or Wi-Fi credentials.

Only the recognition DB on SD is intentionally persistent product data.

## Security boundary

Passive observation, identification, protocol parsing, and authorized device interaction are allowed.

Do not add Wi-Fi deauthentication, credential cracking/harvesting, rogue authentication, BLE hijacking, Zigbee key extraction, TLS MITM, poisoning for credentials, exploit delivery, or persistence on third-party devices.

## Completion criteria

The integration branch can be called **software-complete / flashable baseline** only when:

```text
boot -> empty idle UI
user Scan
→ touch locked
→ D scan gate acquired
→ B runs every enabled module
→ B parses/dedups
→ C recognizes or fails closed
→ matched semantic results queued to A owner task
→ A Device/Entity/State updated
→ E cards appear while progress advances
→ all enabled modules terminal
→ progress 100%
→ D cleanup succeeds and gate releases
→ touch unlocks
→ user can open generic detail/entity views
```

and:

```text
Settings
→ Web Management
→ temporary SoftAP
→ Wi-Fi session connection OR Database import
→ prevalidate
→ explicit FORMAT ENTIRE SD confirmation
→ bounded streaming upload
→ integrity verification
→ atomic final rename
→ DB status refresh
```

and all integrated ESP-IDF v6.1 CI jobs are green and flashable artifacts are produced.

After reaching this state, stop feature expansion. Report remaining physical `BENCH_REQUIRED` items separately instead of adding speculative architecture.
