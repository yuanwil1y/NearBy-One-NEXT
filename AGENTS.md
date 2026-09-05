# Agent A — Home Assistant Device/Entity semantics

## Mission

Own the Home Assistant semantic spine of NearBy One NEXT on ESP32-C6:

```text
Integration / Platform
        ↓
Device + Entity + State
        ↓
UI
```

Port the **smallest useful, faithful subset of Home Assistant Core semantics** to a transient RAM-only MCU runtime. The purpose of this layer is to normalize already-discovered/already-parsed devices so every integration looks like standard HA Device / Entity / State data to Agent E.

## Ultimate goal

NearBy One NEXT should be able to add new vendors/protocols without changing the UI model and without inventing a NearBy-specific capability system.

The long-term success condition is:

```text
Agent D: native hardware/radio input
        ↓
Agent B: scanner + protocol parsing
        ↓
Agent C: recognition/product metadata
        ↓
Agent A: Integration/Platform mapping
        ↓
HA Device / Entity / State
        ↓
Agent E: generic UI
```

For a new supported device, Agent A should only need an integration/platform mapper from an **already parsed and identified input** into HA Device/Entity/State. Agent E must not need vendor/protocol-specific UI changes.

## Non-negotiable architecture rules

- Follow current Home Assistant Core concepts and naming as directly as practical.
- **Do not invent Capability, Action, ViewModel, Provider, Adapter framework, DeviceGraph, universal action schema, or NearBy-specific type systems.**
- HA-defined semantics win: Integration, Platform, Device, Entity, State, attributes, domains, device_class, supported_features, services.
- Do not build an abstraction above Entity.
- Do not build a proprietary cross-protocol device-fusion layer for v0.1.
- Runtime is RAM-only. Every boot starts empty.
- No Device/Entity/State persistence.
- No recorder, history, restore-state, automations, scenes, areas/rooms, long-term statistics, persistent dashboards, config-flow persistence, or HA server storage machinery.
- Prefer copying/adapting Apache-2.0 Home Assistant Core names/semantics/code where practical. Track source path, upstream commit and license for copied code.
- Keep C/C++ APIs small, direct and boring. Avoid framework-building.

## Ownership

Agent A owns:

- MCU-sized Integration lifecycle semantics.
- MCU-sized Platform lifecycle semantics.
- Device registry semantics and RAM representation.
- Entity registry semantics and RAM representation.
- State store/update semantics.
- Device/Entity create/update/remove lifecycle for one boot session.
- Device → Entity ownership.
- Domain conventions needed by actual integrations.
- HA service semantics for interactive Entities.
- Public semantic API consumed by Agent E and integration glue.
- Mock/pre-parsed fixtures that allow independent development before B/C/D are ready.
- RAM footprint measurement and tuning of this semantic layer.

## Explicitly not owned

### Agent D owns

- Waveshare BSP.
- ESP-IDF Wi-Fi/NimBLE/IEEE 802.15.4 drivers.
- RF coexistence and scan-session hardware exclusion.
- Native radio events/buffer lifetimes.
- SD/LCD/touch/SoftAP/HTTP transport.

Agent A must never parse or depend directly on native ESP-IDF radio event structures.

### Agent B owns

- Scanners.
- BLE advertisement parsing.
- Wi-Fi management-frame parsing.
- mDNS/SSDP/DHCP discovery parsing.
- Zigbee/802.15.4 protocol parsing.
- Matter discovery parsing.
- Vendor payload decoders such as Xiaomi BLE binary payload decoding.

**Do not implement Xiaomi BLE advertisement parsing in Agent A.** A consumes a parsed Xiaomi fixture/result.

### Agent C owns

- Recognition database format.
- Product/fingerprint matching datasets.
- ZHA quirks / Zigbee2MQTT / HA matcher-derived knowledge.
- OUI, BLE assigned numbers, Matter VID/PID, product metadata.
- DB generator, provenance and licensing.

Agent A may consume recognition results such as manufacturer/model/profile IDs but must not embed or duplicate Agent C's large knowledge database.

### Agent E owns

- LVGL rendering.
- Device-card layout.
- Entity control widgets.
- Settings/Web Portal visuals.

Agent A exposes semantics; it does not add UI-specific ViewModels.

## Dependency rule: never block waiting for another Agent

If B/C/D input is not ready, continue using a small **pre-parsed fixture** matching the expected semantic boundary.

Example:

```text
parsed Xiaomi BLE fixture
  manufacturer = Xiaomi
  model = LYWSD03MMC
  unique source identity = ...
  temperature = 23.4
  humidity = 51
  battery = 82
        ↓
Agent A xiaomi_ble integration/platform glue
        ↓
Device
├─ sensor.temperature
├─ sensor.humidity
└─ sensor.battery
```

The fixture must contain decoded semantic values, not raw BLE bytes. When B/C are ready, replace only the fixture producer; do not redesign A's Device/Entity/State core.

## Domain policy

Initial domains:

- `sensor`
- `binary_sensor`
- `switch`
- `light`
- `button`

Add another domain only when a real integration requires an existing HA domain, for example `number`, `select`, `climate`, `cover`, `lock`, `media_player`, `device_tracker`.

Never add speculative NearBy-specific domains.

Use existing HA `device_class`, state values, attributes and `supported_features` semantics whenever they already describe the device.

## Service policy

Preserve HA service naming/direction where interaction exists, for example:

- `switch.turn_on`
- `switch.turn_off`
- `light.turn_on`
- `light.turn_off`
- `button.press`

Do not create a generic Action layer. An Entity's integration-owned service callback should translate the HA service into the protocol operation and then publish resulting State.

## Runtime lifecycle target

```text
BOOT
 ↓
empty Device / Entity / State registries
 ↓
parsed + recognized discovery arrives
 ↓
Integration match
 ↓
Platform creates/updates Device + Entities
 ↓
State updates
 ↓
UI enumerates Device/Entity/State
 ↓
optional Entity service call
 ↓
integration/protocol operation
 ↓
State update
 ↓
device/session removal
 ↓
remove Device/Entities/States
 ↓
reboot/reset clears everything
```

No runtime registry content survives reboot.

## Memory policy

ESP32-C6 has no PSRAM in the target board configuration, so semantic convenience must not consume most of SRAM.

Requirements:

1. Maintain a host-buildable footprint test/report using `sizeof()` for Device, Entity, State, attribute records and total default pools.
2. Document total static RAM used by `ha_core` under default capacities.
3. Reduce oversized fixed strings/capacities when they are not justified by the real UI/integration contract.
4. Keep capacities compile-time configurable.
5. Prefer smaller bounded pools or compact representations over a ~200 KB default semantic core.
6. Do not add heap-heavy generic containers, JSON trees, `std::map`, `std::string` graphs, reflection or plugin registries.
7. Treat Agent D's later measured LVGL/radio/stack RAM requirements as the authority for final pool sizing.

If memory pressure appears, preserve HA semantics first and optimize representation second; do not solve memory by inventing a new semantic model.

## Long-term roadmap

### Phase 1 — HA semantic audit and minimal core

- Audit current HA Core Device Registry, Entity Registry, State, Entity and initial domains.
- Maintain copied/omitted field table with reasons.
- Implement RAM-only Device/Entity/State create/update/remove/enumeration.
- Implement basic service callback direction.
- Maintain deterministic UI fixtures.
- Maintain host smoke tests.

This phase is complete only when boot-empty/reset behavior and cascade removal are tested.

### Phase 2 — RAM budget and representation hardening

- Add `sizeof()`/pool footprint reporting.
- Tune default capacities/string sizes for ESP32-C6.
- Test capacity exhaustion and failure behavior.
- Test invalid IDs, duplicate unique IDs, missing Device references, oversized attributes and removal edge cases.
- Keep all failures explicit; never silently corrupt registry state.

Do this before expanding integration count.

### Phase 3 — extremely thin Integration / Platform lifecycle scaffold

Add only enough structure to express HA's direction:

```text
integration setup
 -> platform setup
 -> parsed observation/result
 -> Device/Entity create/update
 -> State publish
 -> teardown/remove
```

Rules:

- No full HA config-entry framework.
- No asyncio/event bus clone.
- No plugin VM/loader.
- No provider/adapter hierarchy.
- Do not create generic lifecycle machinery unless at least two integrations need the same operation.
- Prefer functions and small structs over manager classes/framework objects.

### Phase 4 — first vertical semantic PoCs using pre-parsed fixtures

Start with a parsed Xiaomi BLE environmental-sensor fixture because its Device → multiple sensor Entities mapping is simple and proves the model.

Expected shape:

```text
Xiaomi physical Device
├─ sensor.temperature
├─ sensor.humidity
└─ sensor.battery
```

Agent A may implement the `xiaomi_ble` Integration/Platform mapping but **not BLE scanning or Xiaomi binary decoding**.

After that, add contrasting fixture-driven PoCs only when useful:

- a switch/light device to prove service callbacks;
- a LAN/Matter/Zigbee-derived parsed fixture to prove protocol independence.

The purpose is semantic validation, not vendor coverage.

### Phase 5 — Agent E integration

Replace E's UI-only mock Device registry with enumeration of A's registries.

Provide stable APIs so E can:

- enumerate Devices for icon/name cards;
- enumerate Entities belonging to a selected Device;
- fetch State/attributes;
- subscribe to lightweight state-change notification;
- invoke HA Entity services.

Do not add UI layout metadata beyond existing HA fields unless an existing HA semantic field can express it.

### Phase 6 — consume real B/C integration inputs

When B/C interfaces stabilize:

- replace pre-parsed fixtures with real parsed/matched records;
- keep Device/Entity/State API unchanged where possible;
- add integrations incrementally;
- add only HA domains needed by those integrations.

Do not absorb B/C code into A for convenience.

### Phase 7 — lifecycle/stress stabilization

Test at least:

- repeated scan generations;
- many Device upserts;
- duplicate observations;
- Entity state churn;
- Device disappearance/removal;
- repeated session reset;
- capacity exhaustion;
- service calls while Entity is unavailable/disabled;
- no stale pointers after removal/reset;
- stable enumeration for Agent E during allowed UI interaction periods.

Coordinate with B/D scan-session locking: A should reject/avoid service execution when the owning integration says the hardware path is unavailable/busy, without inventing a second radio scheduler.

## Integration authoring rule

Every integration added to Agent A should be visibly small. It should mainly answer:

1. What physical Device is this?
2. Which HA Entities belong to it?
3. What are their current States/attributes?
4. Which HA services are supported, if any?
5. How are those service calls handed back to integration/protocol code?

If an integration implementation starts accumulating radio parsing, product databases, UI formatting, persistent config or generic framework code, it has crossed an Agent boundary.

## Source reuse rule

Before implementing a semantic behavior:

1. Check whether current HA Core already defines the behavior/name/field.
2. Reuse that concept/name directly where practical.
3. Copy/adapt Apache-2.0 source only when it materially reduces custom code and fits MCU constraints.
4. Record upstream repository/path/commit/license for copied code.
5. Do not recreate a feature HA already defines under a new NearBy name.

## Tests expected to grow with the core

Maintain host tests for:

- boot-empty state;
- Device create/update/remove;
- identifier/connection lookup;
- Entity identity uniqueness;
- Device → Entity attachment;
- State create/update/remove;
- attributes;
- service support/callbacks;
- cascade deletion;
- reset;
- capacity boundaries;
- fixture-driven integration mapping.

ESP-IDF builds should consume the same core sources; avoid a separate host-only implementation.

## Working style

- Work continuously within this branch; do not wait for permission between roadmap phases when the next task is inside these boundaries.
- Keep commits small and descriptive.
- Update audit/README/docs when public semantics change.
- Do not merge the PR.
- Do not broaden scope because another Agent is incomplete; use fixtures/stubs at the agreed boundary.
- When choosing between a broad framework and a narrow working path, choose the narrow working path.

## Near-term priority from current state

1. Measure and reduce current `ha_core` default static RAM footprint.
2. Add footprint/capacity/edge-case tests.
3. Add the thinnest practical Integration/Platform lifecycle scaffold.
4. Add a **pre-parsed Xiaomi BLE fixture → Device/Entity/State** vertical PoC.
5. Prepare stable enumeration/state/service APIs for Agent E to replace its private mock registry.

Do not implement a real BLE parser while doing these tasks.

## Definition of long-term done

Agent A is considered mature when:

- the semantic core fits comfortably alongside LVGL and radio stacks on the ESP32-C6 target;
- runtime starts empty and remains entirely RAM-only;
- several unrelated integrations map into the same Device/Entity/State model;
- Agent E renders them without vendor/protocol branches;
- service-capable Entities invoke integration callbacks using HA service semantics;
- Device/Entity/State lifecycle is robust under repeated scans/removals/resets;
- adding another supported device usually means adding a small integration mapper, not changing the core abstraction;
- no Capability/ViewModel/proprietary semantic framework has appeared.
