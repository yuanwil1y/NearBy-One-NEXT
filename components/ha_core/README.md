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

## Task / thread ownership contract

`ha_core` is deliberately **single-owner-task and not thread-safe**. This is part of the frozen public contract, not a temporary implementation detail.

One application-selected owner task must perform all runtime access that touches Device / Entity / State pools or callbacks:

- `ha_device_*`, `ha_entity_*`, and `ha_state_*` mutation and lookup/enumeration;
- Agent E Device/Entity/State enumeration and State reads;
- `ha_state_set_listener()` registration;
- `ha_entity_supports_service()` / `ha_entity_call_service()`;
- `ha_core_reset()` and `ha_core_revision()` sampling.

If Agent B/C processing happens in other FreeRTOS tasks, those tasks **must not call `ha_core` directly**. They copy their already-matched, already-parsed semantic result into an application-owned queue. The owner task dequeues that value and makes the normal direct calls:

```text
B/C worker task(s)
  semantic Device/Entity/State value
              |
              | copy through existing app/FreeRTOS queue
              v
        single ha_core owner task
          |               |
          | mutate        | enumerate/read for Agent E
          v               v
      Device/Entity/State runtime
```

The queue is outside `ha_core`; this component does not add a queue manager, dispatcher, mutex, semaphore, critical section, atomic wrapper, or task framework.

### `ha_core_revision()` is not synchronization

`ha_core_revision()` is only an invalidation counter for code already running on the owner task. It may be sampled to decide whether a previously resolved borrowed view should be re-resolved after owner-task code performed a successful mutation.

It is **not atomic**, not a lock, not a memory barrier, not a seqlock, and not permission for another task to read the pools concurrently. A pattern such as "worker mutates while UI reads, then UI retries if revision changed" is explicitly unsupported.

Pointers returned by getters/enumerators are borrowed read-only views into fixed pools. Copy durable keys such as `device.id` or `entity.entity_id` when needed, and re-resolve after any successful owner-task mutation.

### Callback ownership

The State listener registered with `ha_state_set_listener()` is invoked synchronously from State publication on the owner task. It should use the supplied State only as a borrowed view and normally mark/invalidate UI work rather than handing the pointer to another task.

Entity service handlers are also invoked synchronously by `ha_entity_call_service()` on the owner task. If the actual protocol control path belongs to another task, the service handler should enqueue a command to that task and return; any resulting semantic State update comes back through the normal queue to the owner task.

The detailed frozen contract and integration examples are in `docs/research/ha-core-task-ownership.md`.

## Production semantic API

Upstream B/C glue should use the direct API instead of introducing another matched-result framework:

1. Resolve an existing Device with `ha_device_get_by_identifier()` / `ha_device_get_by_connection()`, or create/update it with `ha_device_upsert()`.
2. Create/update Entities with `ha_entity_upsert()`. Entity identity is `domain + platform + unique_id`; `entity_id` must be a valid HA-style `<domain>.<slug>` for one of the supported domains.
3. Publish `entity_id + state + attributes` with `ha_state_set()`.
4. Remove an Entity with `ha_entity_remove()` or a Device with `ha_device_remove()`. Device removal cascades to attached Entities and States.
5. Reset Device / Entity / State RAM with `ha_core_reset()` between boot/runtime sessions.

A Device must carry at least one HA-style `identifier` or `connection`. Duplicate identifiers or connections across different Devices are rejected, matching the collision boundary in HA Device Registry semantics.

No production API accepts packet bytes, scanner records, product fingerprints or recognition records.

## Stable API for Agent E

Agent E consumes A directly, on the same owner task:

- Nearby list: `ha_device_count()` + `ha_device_at()`.
- Selected Device: `ha_entity_count_for_device(device_id)` + `ha_entity_at_for_device(device_id, index)`.
- State: `ha_state_get(entity_id)`.
- State refresh: `ha_state_set_listener()` for State publish/update notification.
- Owner-task invalidation detection: `ha_core_revision()`.
- Interaction: `ha_entity_supports_service()` + `ha_entity_call_service()`.

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

## Generic semantic fixtures are test-only

Protocol/vendor-neutral fixtures live under `tests/` and are not linked by the ESP-IDF component. `tests/ha_core_semantic_path.c` proves the already-matched semantic boundary with direct calls only.

There is no vendor parser or recognition code in this component.

## Host tests

From repository root:

```sh
./tests/run_ha_core_tests.sh
```

This runs:

- normal lifecycle smoke coverage;
- `sizeof()` / pool footprint reporting and the default <32 KiB regression guard;
- generic already-matched semantic input -> Device/Entity/State coverage;
- tiny-capacity exhaustion and edge-case coverage;
- a deliberately single-threaded owner-task harness proving queued-value -> mutation -> UI enumeration and revision invalidation behavior;
- a source guard that fails if lock/atomic synchronization primitives are added to production `ha_core.c/.h`.

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

## Freeze status

After the task/thread ownership contract and its host regression test land, `agent-a/home-assistant-audit` is a **frozen baseline**. No further feature development is intended on this branch unless the project explicitly reopens Agent A for a correctness defect or a required HA-semantic compatibility change. The branch is not merged by Agent A.
