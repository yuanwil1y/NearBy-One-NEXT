# Agent A — Home Assistant Device/Entity semantics

## Mission
Port the smallest useful subset of Home Assistant Core's runtime semantics to ESP32-C6: `Integration -> Platform -> Device + Entity + State -> UI`.

## Non-negotiable rules
- Follow current Home Assistant Core concepts and naming as directly as practical.
- **Do not invent Capability, Action, ViewModel, Provider, device graph, or NearBy-specific type systems.**
- HA-defined semantics win: Device, Entity, State, attributes, domains, device_class, services.
- Runtime is RAM-only. Every boot starts empty. No persistence for Device/Entity/State.
- Do not implement recorder, history, restore-state, automations, scenes, areas/rooms, long-term statistics, persistent dashboards, or HA's large config machinery.
- Current HA behavior that devices belong to one config entry/subentry means v0.1 does not need proprietary cross-protocol identity fusion.
- Prefer copying/adapting Apache-2.0 Home Assistant Core code/field names over rewriting concepts from scratch. Track copied source path/commit/license.

## Own
- Integration and Platform lifecycle subset.
- Device registry semantics and MCU field subset.
- Entity registry semantics and MCU field subset.
- State store/update semantics.
- Domain conventions needed by UI: start with `sensor`, `binary_sensor`, `switch`, `light`, `button`; add only when an integration needs more.
- Service semantics such as `switch.turn_on/off`, `light.turn_on/off`, `button.press` where interaction exists.
- Device/Entity create-update-remove lifecycle during one boot session.
- A small C/C++ API usable by Agent B/C/E without Python/runtime dependencies.

## Do not own
- Radio drivers or board support (D).
- Scanner/protocol parsing (B).
- Recognition database and ecosystem datasets (C).
- LVGL rendering (E).

## First deliverables
1. Audit current HA Core source for Device Registry, Entity Registry, State, Integration/Platform lifecycle and domain base semantics.
2. Produce a precise MCU subset table: copied field / omitted field / reason.
3. Implement or scaffold the RAM-only Device, Entity and State structures with stable HA-like names.
4. Provide mock Device/Entity/State fixtures for Agent E.
5. Document public interfaces B/C can call to create/update/remove Devices and Entities.

## Engineering constraints
ESP-IDF, C/C++, fixed/small allocations where practical, no Python runtime, no SQLite runtime registry. Keep APIs boring and direct. Avoid framework-building.

## Definition of done for first pass
A small host-testable or ESP-IDF-compilable semantic core can create a Device, attach multiple Entities, update State/attributes, enumerate them for UI, invoke basic service callbacks, and clear everything on reboot/session reset.