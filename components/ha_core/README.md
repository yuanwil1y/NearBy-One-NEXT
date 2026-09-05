# ha_core

Small RAM-only Home Assistant Device / Entity / State semantic subset for NearBy One NEXT.

The component is intentionally boring: fixed-size C structs, no heap ownership, no persistence, no Python runtime, no SQLite, and no NearBy-specific capability model.

## Runtime contract

Integration / parser code should use this flow:

1. Resolve an existing device with `ha_device_get_by_identifier()` / `ha_device_get_by_connection()` or create/update one with `ha_device_upsert()`.
2. Create/update entities with `ha_entity_upsert()`. Entity identity follows HA's `domain + platform + unique_id`; the public `entity_id` must start with the same domain.
3. Publish the UI-facing HA state triplet with `ha_state_set(entity_id, state, attributes, count)`.
4. UI code enumerates `ha_device_at()`, `ha_entity_at()`, `ha_state_at()` or uses direct getters. The UI must not depend on scanner/protocol records.
5. Interactive entities register `service_handler` and receive HA service names through `ha_entity_call_service()`.
6. A boot/session reset calls `ha_core_reset()` and starts empty.

Removing a Device cascades removal of attached Entities and their States. Removing an Entity removes its State.

## First domains and services

| domain | runtime state | services in v0.1 |
|---|---|---|
| `sensor` | free-form state string + attributes | none |
| `binary_sensor` | normally `on` / `off` | none |
| `switch` | normally `on` / `off` | `turn_on`, `turn_off` |
| `light` | normally `on` / `off` + attributes such as brightness | `turn_on`, `turn_off` |
| `button` | transient/unknown state; action is service-driven | `press` |

This intentionally omits HA's full service registry, event bus, feature schemas, config flows and platform machinery. Integration-specific code owns protocol translation and registers a direct callback on interactive entities.

## Fixed-size tuning

Default capacities live in `ha_core.h` and may be overridden by compile definitions:

- `HA_MAX_DEVICES=24`
- `HA_MAX_ENTITIES=96`
- `HA_MAX_STATES=HA_MAX_ENTITIES`
- `HA_MAX_ATTRIBUTES=8` per state
- `HA_MAX_IDENTIFIERS=4` per device
- `HA_MAX_CONNECTIONS=4` per device

State/attribute values are stored as UTF-8 strings. This keeps the MCU representation small and mirrors the part of HA State the UI needs (`entity_id`, `state`, `attributes`) without porting Python's arbitrary-value containers.

## UI mock fixtures

`ha_mock_fixtures_load()` resets the runtime and loads deterministic demo data spanning all first-pass domains:

- `sensor.living_room_temperature`
- `binary_sensor.living_room_motion`
- `switch.desk_plug`
- `light.desk_lamp`
- `button.desk_identify`

Agent E can call the fixture loader before rendering and then enumerate normal Device / Entity / State APIs; no UI-only ViewModel is introduced.

## Host smoke test

From repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -Icomponents/ha_core/include \
  components/ha_core/ha_core.c \
  components/ha_core/ha_mock_fixtures.c \
  tests/ha_core_smoke.c \
  -o /tmp/ha_core_smoke
/tmp/ha_core_smoke
```

ESP-IDF consumes the same sources through `components/ha_core/CMakeLists.txt`.

## Upstream semantics / provenance

The field selection and naming were audited against Home Assistant Core `dev` at commit `9b312180c69890fcad3d0adea76ccfd46b339544` on 2026-09-06, especially:

- `homeassistant/helpers/device_registry.py`
- `homeassistant/helpers/entity_registry.py`
- `homeassistant/helpers/entity.py`
- `homeassistant/core.py`
- `homeassistant/components/sensor/`
- `homeassistant/components/binary_sensor/`
- `homeassistant/components/switch/`
- `homeassistant/components/light/`
- `homeassistant/components/button/`

This component is a clean C MCU implementation of the audited semantics/field names; it does not copy Python source bodies.
