# Agent A — Home Assistant Device / Entity / State core

## Mission

Agent A owns one narrow job: **extract and adapt the smallest useful subset of Home Assistant Core that turns already-matched, already-parsed device data into Home Assistant-style Device / Entity / State runtime objects on ESP32-C6.**

The boundary is:

```text
already matched + already parsed semantic input
                    ↓
               Agent A
                    ↓
          Device + Entity + State
                    ↓
                 Agent E UI
```

Agent A does **not** discover, recognize, fingerprint, decode or classify devices. It only provides the HA semantic/runtime layer after those jobs are complete.

## Ultimate goal

NearBy One NEXT should have a compact, RAM-only Home Assistant semantic core whose public behavior remains stable regardless of protocol or vendor.

When upstream code says, in semantic terms:

```text
Device
  name = "Temperature Sensor"
  manufacturer = "Vendor"
  model = "Model X"

Entities
  sensor.temperature = 23.4 °C
  sensor.humidity = 51 %
```

Agent A must be able to represent that as normal HA-style Device / Entity / State data without knowing whether the source was BLE, Wi-Fi, Zigbee, Thread, Matter, mDNS, SSDP or anything else.

The architecture is:

```text
Agent D: hardware / native radio input
        ↓
Agent B: scanner + protocol parsing
        ↓
Agent C: recognition / matching / product knowledge
        ↓
semantic matched result
        ↓
Agent A: HA Device / Entity / State core
        ↓
Agent E: generic UI
```

## Non-negotiable rules

- Follow current Home Assistant Core semantics and naming as directly as practical.
- Prefer reusing/copying/adapting HA Core Apache-2.0 concepts/code over inventing replacements.
- **Do not invent Capability, Action, ViewModel, Provider, Adapter framework, DeviceGraph, universal action schema, or NearBy-specific semantic types.**
- Device is the physical grouping.
- Entity is the state/control unit.
- State is `entity_id + state + attributes`.
- Runtime is RAM-only. Every boot starts empty.
- No Device/Entity/State persistence.
- No recorder, history, restore-state, automations, scenes, areas/rooms, long-term statistics, persistent dashboards, config-flow persistence, SQLite registry, or HA server storage machinery.
- No proprietary cross-protocol device fusion.
- Keep C/C++ APIs small, direct and boring.
- Optimize representation for ESP32-C6 SRAM without changing HA semantics.

## Agent A owns

### 1. HA source audit and extraction

Study and continuously track the relevant parts of Home Assistant Core, especially:

- `homeassistant/helpers/device_registry.py`
- `homeassistant/helpers/entity_registry.py`
- `homeassistant/helpers/entity.py`
- `homeassistant/core.py` State semantics
- `DeviceInfo`
- domain semantics required by real matched inputs

For each useful HA field/behavior, decide:

- keep/copy/adapt;
- represent more compactly;
- omit because it only exists for HA server persistence/config/history.

Maintain a precise source/field subset document and provenance for copied code.

### 2. RAM-only Device registry subset

Provide the smallest useful Device representation and lifecycle:

- create/upsert;
- lookup;
- identifiers/connections where semantically useful;
- name/manufacturer/model/model_id and other justified HA fields;
- attach Entities;
- remove;
- cascade cleanup;
- reset entire session.

### 3. RAM-only Entity registry subset

Provide the smallest useful Entity representation and lifecycle using HA semantics:

- `entity_id`;
- `unique_id`;
- `domain`;
- `platform` only if needed to preserve HA identity semantics;
- `device_id`;
- `device_class`;
- `entity_category` if justified;
- `name`;
- `icon` if useful;
- `unit_of_measurement`;
- `supported_features` where needed;
- enabled/available runtime state where needed;
- create/upsert/remove/enumerate.

### 4. RAM-only State store

Provide:

- `entity_id`;
- state string;
- bounded attributes;
- create/update/remove;
- lightweight notification for UI refresh;
- full reset on boot/session reset.

Do not port HA's full event bus or history/context machinery.

### 5. Existing HA domain/service semantics

Start with:

- `sensor`
- `binary_sensor`
- `switch`
- `light`
- `button`

Add another domain only when upstream matched semantic data actually requires an existing HA domain such as `number`, `select`, `climate`, `cover`, `lock`, `media_player`, or `device_tracker`.

Preserve HA service names where interaction exists, for example:

- `switch.turn_on`
- `switch.turn_off`
- `light.turn_on`
- `light.turn_off`
- `button.press`

Do not add a generic Action abstraction.

### 6. Stable public semantic API

Provide Agent E and upstream glue with a small API that can:

```text
upsert Device
upsert Entity
set State/attributes
remove Entity/Device
reset session
enumerate Devices
enumerate Entities for a Device
get State
subscribe to lightweight State changes
invoke an HA Entity service callback
```

The API accepts **semantic data only**. It must not accept raw BLE advertisements, Wi-Fi frames, Zigbee frames, SSDP packets, mDNS records, vendor payload bytes, recognition DB records, or protocol-specific scanner structures.

## Explicitly NOT owned

### Agent D

Owns:

- Waveshare BSP;
- ESP-IDF Wi-Fi/NimBLE/IEEE 802.15.4;
- LCD/touch/SD;
- RF coexistence;
- scan-session hardware exclusion;
- SoftAP/HTTP transport;
- native event/buffer lifetime.

Agent A never depends directly on native radio event structures.

### Agent B

Owns:

- scanning;
- protocol discovery;
- BLE advertisement parsing;
- Wi-Fi frame parsing;
- mDNS/SSDP/DHCP parsing;
- Zigbee/802.15.4 parsing;
- Matter discovery parsing;
- vendor binary payload decoders.

Agent A must not contain BLE/Wi-Fi/Zigbee/Matter packet parsing or vendor decoder code.

### Agent C

Owns:

- recognition/matching database;
- fingerprints/product tables;
- HA discovery match data;
- ZHA quirks / Zigbee2MQTT-derived knowledge;
- OUI/BLE/Matter/Zigbee assigned-number data;
- product/vendor recognition metadata;
- database generation/format/provenance.

Agent A consumes the **result** of matching, never the matching database itself.

### Agent E

Owns:

- LVGL UI;
- Device cards;
- Entity widgets;
- Settings/Web Portal visuals.

Agent A exposes HA semantics only and never adds a UI-specific ViewModel.

## Critical boundary rule

Agent A must **not create vendor-specific integrations or parser modules** such as:

```text
xiaomi_ble/
switchbot/
shelly/
```

unless the only content is a temporary test fixture outside production runtime. Production vendor/protocol interpretation belongs upstream in B/C.

A test fixture should be generic semantic input, for example:

```text
matched_device_fixture
  name = "Test Sensor"
  manufacturer = "Test Vendor"
  model = "T100"

matched_entities
  sensor.temperature = 23.4 °C
  sensor.humidity = 51 %
  sensor.battery = 82 %
```

The purpose is to prove Device/Entity/State behavior, not vendor support.

## Runtime lifecycle

```text
BOOT
 ↓
empty Device / Entity / State
 ↓
upstream matched semantic result arrives
 ↓
Device upsert
 ↓
Entity upsert(s)
 ↓
State publish/update
 ↓
Agent E enumerates/renders
 ↓
optional HA service callback
 ↓
upstream protocol layer performs action
 ↓
State updated
 ↓
Device disappears/session ends
 ↓
remove/reset
```

Agent A does not decide how a device was found, matched, decoded or controlled over the radio.

## Memory policy

The target ESP32-C6 has no PSRAM. The semantic core must leave substantial SRAM for LVGL, Wi-Fi, NimBLE, 802.15.4, FreeRTOS stacks, SD/FATFS and scanner buffers.

Requirements:

1. Maintain host-buildable `sizeof()` / pool footprint reporting.
2. Document total static RAM under default capacities.
3. Keep capacities compile-time configurable.
4. Reduce unjustified fixed string sizes/capacities.
5. Test capacity exhaustion explicitly.
6. Avoid heap-heavy generic containers, JSON trees, reflection, plugin registries and manager frameworks.
7. Treat Agent D's measured system RAM budget as authority for final sizing.

Do not solve RAM pressure by inventing a different semantic model.

## Long-term roadmap

### Phase 1 — HA semantic audit

- Track current HA Device Registry / Entity Registry / State behavior.
- Maintain kept/omitted field tables.
- Record upstream path/commit/license.
- Identify exact code/semantics that can be copied or adapted.

### Phase 2 — minimal RAM-only core

- Device create/update/remove/enumerate.
- Entity create/update/remove/enumerate.
- Device → Entity ownership.
- State create/update/remove.
- cascade cleanup.
- reset to boot-empty.
- basic HA service callback semantics.

### Phase 3 — RAM hardening

- measure struct/pool sizes;
- reduce default footprint;
- test boundary/capacity behavior;
- test duplicate identity and invalid references;
- test reset/removal safety;
- ensure no stale pointers or corrupt registry state.

### Phase 4 — generic matched-input fixture contract

Define the thinnest practical semantic ingestion shape needed to populate Device/Entity/State from already-matched data.

Do **not** turn this into a new abstraction framework. If direct calls such as `ha_device_upsert()`, `ha_entity_upsert()`, and `ha_state_set()` are sufficient, prefer those over adding another layer.

Use generic fixtures only to test the boundary.

### Phase 5 — Agent E integration

Make E consume A directly:

- enumerate Devices for the Nearby list;
- enumerate Entities attached to a selected Device;
- fetch States/attributes;
- receive lightweight state-change notification;
- invoke HA Entity services.

E should not need its own runtime Device registry once this path is working.

### Phase 6 — consume real B/C output

When B/C stabilize their output, connect that already-matched semantic result to the existing A API with the smallest glue possible.

Rules:

- do not absorb B/C parsers or DB logic;
- do not add vendor-specific branches to the core;
- do not redesign Device/Entity/State because a protocol is different;
- only add an existing HA field/domain when real semantic data requires it.

### Phase 7 — lifecycle/stress stabilization

Test:

- repeated scan generations;
- many Device/Entity upserts;
- duplicate semantic observations;
- State churn;
- Device disappearance/removal;
- repeated reset;
- capacity exhaustion;
- service calls on disabled/unavailable Entities;
- stable enumeration for UI.

## Source reuse rule

Before implementing any semantic behavior:

1. Check whether current Home Assistant Core already defines it.
2. Reuse the same name and semantics where practical.
3. Copy/adapt Apache-2.0 source when that genuinely reduces custom code and fits MCU constraints.
4. Record upstream repository/path/commit/license.
5. Never recreate an HA concept under a NearBy-specific name.

## Working style

- Work continuously within this branch without asking for the next task when the next roadmap item is inside these boundaries.
- If B/C/D are not ready, use generic semantic fixtures; do not wait and do not implement their work.
- Keep commits small and descriptive.
- Update audit/README/docs when public semantics change.
- Do not merge the PR.
- When choosing between a broad framework and a narrow direct implementation, choose the narrow direct implementation.

## Near-term priority

1. Measure and reduce current `ha_core` static RAM footprint.
2. Add footprint/capacity/edge-case tests.
3. Continue auditing HA Core for Device/Entity/State code that can be copied/adapted or safely omitted.
4. Define/prove the minimal **generic already-matched semantic input → Device/Entity/State** path using fixtures only.
5. Stabilize enumeration/state/service APIs for Agent E.
6. Prepare A so real B/C output can plug in later without changing the core.

**Do not implement Xiaomi BLE or any other vendor/protocol-specific production path.**

## Definition of long-term done

Agent A is mature when:

- the HA semantic core fits comfortably alongside the rest of the ESP32-C6 system;
- every boot starts with empty RAM-only registries;
- already-matched semantic input can reliably produce HA Device/Entity/State objects;
- Agent E renders Devices and Entities directly from A without vendor/protocol logic;
- service-capable Entities expose HA service semantics through a thin callback;
- lifecycle/reset/removal/capacity behavior is robust;
- adding support for a new vendor/protocol normally changes B/C data/parsing, not A's core;
- no Capability/ViewModel/proprietary semantic framework exists.
