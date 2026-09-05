# ha_core

Compact RAM-only Home Assistant Device / Entity / State semantics for NearBy One NEXT.

`ha_core` starts **after** discovery, parsing and recognition. It accepts semantic data only and has no BLE, Wi-Fi, Zigbee, Matter, mDNS, SSDP, vendor payload or recognition-database dependency.

```text
already matched + already parsed semantic data
                    |
                    v
             ha_device_upsert
             ha_entity_upsert
               ha_state_set
                    |
                    v
              Agent E UI
```

The component intentionally uses fixed-size C pools, no heap ownership, no persistence, no Python runtime, no SQLite, and no NearBy-specific Capability/Action/ViewModel layer.

## Production semantic API

Upstream B/C glue should normally use the direct API instead of introducing another matched-result framework:

1. Resolve an existing Device with `ha_device_get_by_identifier()` / `ha_device_get_by_connection()`, or create/update it with `ha_device_upsert()`.
2. Create/update Entities with `ha_entity_upsert()`. Entity identity is `domain + platform + unique_id`; `entity_id` must be a valid HA-style `<domain>.<slug>` for one of the supported domains.
3. Publish `entity_id + state + attributes` with `ha_state_set()`.
4. Remove an Entity with `ha_entity_remove()` or a Device with `ha_device_remove()`. Device removal cascades to attached Entities and States.
5. Reset Device / Entity / State RAM with `ha_core_reset()` between boot/runtime sessions.

A Device must carry at least one HA-style `identifier` or `connection`. Duplicate identifiers or connections across different Devices are rejected, matching the collision boundary in HA Device Registry semantics.

No production API accepts packet bytes, scanner records, product fingerprints or recognition records.

## Stable API for Agent E

Agent E should consume A directly:

- Nearby list: `ha_device_count()` + `ha_device_at()`.
- Selected Device: `ha_entity_count_for_device(device_id)` + `ha_entity_at_for_device(device_id, index)`.
- State: `ha_state_get(entity_id)`.
- State refresh: `ha_state_set_listener()` for State publish/update notification.
- Structural invalidation: `ha_core_revision()` changes after every successful Device/Entity/State mutation and reset.
- Interaction: `ha_entity_supports_service()` + `ha_entity_call_service()`.

Pointers returned by getters/enumerators are **borrowed read-only views into fixed pools**. Do not cache them across a successful mutating API call. For an enumeration pass that may race with semantic updates, E can sample `ha_core_revision()` before and after the pass and retry if it changed. The core itself does not add a UI ViewModel or a locking framework.

`ha_core_reset()` clears Device/Entity/State data but preserves the registered State listener, so a UI subscription can survive repeated scan/session resets. Pass `NULL` to `ha_state_set_listener()` to unsubscribe.

## Domains and services

| domain | runtime state | services currently exposed |
|---|---|---|
| `sensor` | free-form state string + attributes | none |
| `binary_sensor` | normally `on` / `off` | none |
| `switch` | normally `on` / `off` | `turn_on`, `turn_off` |
| `light` | normally `on` / `off` + attributes | `turn_on`, `turn_off` |
| `button` | transient/unknown state | `press` |

Service callbacks are direct per-Entity callbacks. No generic Action abstraction or full HA service registry is implemented.

## RAM policy and current default footprint

Default compile-time capacities:

- `HA_MAX_DEVICES=8`
- `HA_MAX_ENTITIES=32`
- `HA_MAX_STATES=HA_MAX_ENTITIES`
- `HA_MAX_ATTRIBUTES=4` per State
- `HA_MAX_IDENTIFIERS=2` per Device
- `HA_MAX_CONNECTIONS=2` per Device

The capacities can be overridden by compiler definitions. Count fields are `uint8_t`, so per-object attribute/identifier/connection capacities must remain <=255.

On the x86_64 host ABI used for the hardening test:

| item | bytes |
|---|---:|
| `ha_device_t` | 451 |
| `ha_entity_t` | 368 |
| `ha_state_t` | 361 |
| Device pool | 3,616 |
| Entity pool | 12,032 |
| State pool | 11,584 |
| total `ha_core` static data | **27,252** |

The host test asserts the default stays below 32 KiB. ESP32-C6 uses smaller pointers than the x86_64 host, so the target value is expected to be no larger, but the ESP-IDF map file remains authoritative when Agent D provides the complete firmware RAM budget.

The pre-hardening layout used 24 Devices, 96 Entities, 96 States, 8 attributes and substantially larger fixed strings, putting the raw pools near 195 KiB on the same 64-bit host ABI. The current layout removes server/config-only fields and reduces default capacities instead of changing HA Device/Entity/State semantics.

## Generic semantic fixtures are test-only

Protocol/vendor-neutral fixtures live under `tests/` and are not linked by the ESP-IDF component. `tests/ha_core_semantic_path.c` intentionally proves the boundary with direct semantic calls such as:

```text
Device(name/manufacturer/model + identifier)
  -> sensor.temperature = 23.4 °C
  -> switch.power = off + turn_on callback
```

There is no Xiaomi/SwitchBot/Shelly/vendor parser or recognition code in this component.

## Host tests

From repository root:

```sh
./tests/run_ha_core_tests.sh
```

This runs:

- normal lifecycle smoke coverage;
- `sizeof()` / pool footprint reporting and the default <32 KiB regression guard;
- generic already-matched semantic input -> Device/Entity/State coverage;
- a deliberately tiny-capacity build that exercises exhaustion and edge cases.

ESP-IDF links only `components/ha_core/ha_core.c` through `components/ha_core/CMakeLists.txt`.

## Upstream semantics / provenance

Current audit pin: `home-assistant/core@2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48` (`dev`, 2026-09-06).

Primary references:

- `homeassistant/helpers/device_registry.py`
- `homeassistant/helpers/entity_registry.py`
- `homeassistant/helpers/entity.py`
- `homeassistant/core.py` (`State`, `valid_entity_id`)
- `homeassistant/components/sensor/`
- `homeassistant/components/binary_sensor/`
- `homeassistant/components/switch/`
- `homeassistant/components/light/`
- `homeassistant/components/button/`

This is a clean C MCU implementation of the audited names/behavior. It does not copy Python source bodies.
