# Home Assistant runtime semantics -> ESP32-C6 MCU subset

Audit pin: `home-assistant/core@2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48` (`dev`, 2026-09-06).

Agent A owns only the semantic runtime boundary:

```text
already matched + already parsed semantic data
                    ↓
               Agent A
                    ↓
          Device + Entity + State
                    ↓
                 Agent E
```

Discovery, packet parsing, vendor decoders and recognition databases are explicitly outside this component.

## Source map

| HA concern | upstream source | MCU treatment |
|---|---|---|
| Device Registry / `DeviceInfo` | `homeassistant/helpers/device_registry.py` | bounded RAM-only Device pool; identifiers/connections + essential display/product fields |
| Device identity collision | `homeassistant/helpers/device_registry.py` | reject duplicate identifier/connection ownership across different runtime Devices |
| Entity Registry | `homeassistant/helpers/entity_registry.py` | bounded RAM-only Entity pool; `domain + platform + unique_id` identity, Device attachment and UI fields |
| Entity ID validation | `homeassistant/core.py::valid_entity_id` | require supported HA domain plus slug-form object id |
| State | `homeassistant/core.py::State` | `entity_id + state + bounded attributes`; omit timestamps/context/event bus |
| Entity runtime behavior | `homeassistant/helpers/entity.py` + domain modules | enabled/available gating and direct HA service callback only |
| UI consumption | HA Device/Entity/State semantics | E enumerates A directly; no ViewModel layer |

## Current HA behavior carried into the MCU subset

The current HA `DeviceInfo` validation requires at least one of `identifiers` or `connections`, and Device Registry defines identifier and connection collision errors. The MCU core now follows that boundary: a Device with neither identity form is rejected, and identity already owned by another Device cannot be re-registered under a different runtime Device id.

HA `State` continues to expose `entity_id`, `state`, `attributes`, timestamps and context. The MCU deliberately keeps only the first three. HA also validates Entity IDs as `<domain>.<entity>` with both parts slugs; the MCU checks the supported domain and a lowercase ASCII slug object id.

## Device field subset

| HA field / behavior | MCU | reason |
|---|---|---|
| runtime `id` | keep | fixed-pool key used by attached Entities; 33-byte buffer holds a 32-char HA-style hex id + NUL |
| `identifiers` | keep, max 2 default | primary semantic Device identity; required when no connection exists |
| `connections` | keep, max 2 default | alternate HA Device identity; collision checked |
| `manufacturer` | keep | Device card metadata |
| `model` | keep | Device card metadata |
| `model_id` | keep | HA product/model identity when upstream semantic data provides it |
| `name` | keep | Device display name |
| `config_entry_id` / `config_subentry_id` | **omit now** | Agent A no longer owns config-entry/integration lifecycle; no persistence/config flow is present |
| `serial_number` | omit until required | useful HA field but not required for the current generic semantic/UI contract; fixed storage cost is unjustified today |
| `sw_version` / `hw_version` | omit until required | same; add exact HA fields later if real semantic inputs/UI require them |
| `configuration_url` | omit | server/web management metadata is not required for 1.9-inch nearby UI core |
| `via_device_id` | omit until required | no proprietary graph/fusion layer; parent/via semantics can be added only for a real HA-semantic input |
| area/labels/name-by-user/timestamps/disabled provenance | omit | persistent HA server organization/customization/history machinery |
| storage/migration/orphan/composite logic | omit | every boot/session starts from empty RAM |

The reduction is a field/capacity trim, not a new Device model.

## Entity field subset

| HA field / behavior | MCU | reason |
|---|---|---|
| `entity_id` | keep | public State/UI key |
| `unique_id` | keep | stable producer-provided identity |
| `platform` | keep | preserves HA Entity Registry identity dimension |
| `domain` | keep | domain behavior/UI selector |
| `device_id` | keep | physical Device -> Entity grouping |
| `device_class` | keep | HA UI semantics such as temperature/motion |
| `name` | keep | display |
| `icon` | keep | optional HA UI metadata |
| `unit_of_measurement` | keep | sensor display |
| `supported_features` | keep | HA feature bitmask for domains that require it |
| `has_entity_name` | keep | HA naming semantics |
| enabled/available | keep as booleans | cheap runtime service/UI gating |
| `config_entry_id` / `config_subentry_id` | **omit now** | config-entry ownership is outside narrowed Agent A runtime boundary |
| `entity_category` | omit until required | no current generic fixture/UI path requires config/diagnostic categorization; saves fixed RAM |
| capabilities/options/aliases/area/categories/labels | omit | generic mappings and persistent registry customization are out of scope |
| translation/original/customized fields | omit | firmware UI can localize without HA backend registry persistence machinery |
| timestamps/hidden/disabled provenance enums | omit | server registry persistence/customization only |

`ha_entity_upsert()` also prevents silently retargeting an existing `entity_id` to a different `domain + platform + unique_id` identity.

## State subset

| HA State field / behavior | MCU | reason |
|---|---|---|
| `entity_id` | keep | primary key |
| `state` | keep as bounded UTF-8 string | HA consumer-facing state semantics are string-based |
| `attributes` | keep, max 4 default key/value pairs | enough for current device class/unit/brightness-style UI semantics without Python `Any` trees |
| derived domain/object id | do not store | Entity already owns domain; object id is derivable |
| `last_changed` / `last_updated` / `last_reported` | omit | history/recorder timing is outside A |
| `context` | omit | no automation/event-bus causality |
| full StateMachine event bus | omit | one State publish listener plus a revision counter is sufficient for UI refresh/invalidation |
| restore state | omit | RAM-only boot/session semantics |

## RAM hardening

### Defaults

```text
HA_MAX_DEVICES      8
HA_MAX_ENTITIES     32
HA_MAX_STATES       32
HA_MAX_ATTRIBUTES   4
HA_MAX_IDENTIFIERS  2
HA_MAX_CONNECTIONS  2
```

All six capacities are compile-time configurable. Per-object count fields use `uint8_t` to avoid host/target `size_t` inflation.

### Measured host footprint

x86_64 host `sizeof()` report after hardening:

```text
ha_device_t=451 bytes
ha_entity_t=368 bytes
ha_state_t=361 bytes
device_pool=3616 bytes
entity_pool=12032 bytes
state_pool=11584 bytes
ha_core_static=27252 bytes
```

`tests/ha_core_footprint.c` guards the default host layout below 32 KiB. ESP32-C6 pointer width is smaller than the host used here; the final ESP-IDF map-file number must still be checked against Agent D's whole-system SRAM budget.

The previous defaults (`24/96/96`, 8 attributes, 4 identifiers/connections and larger fixed strings plus config/server metadata) placed the raw pools around 195 KiB on the same host ABI. The hardening pass therefore removes the dominant static-RAM risk without changing the semantic boundary.

## Generic already-matched semantic path

No new ingestion framework is added. Direct calls are the contract:

```text
semantic producer
  -> ha_device_get_by_identifier()/ha_device_get_by_connection()
  -> ha_device_upsert()
  -> ha_entity_get_by_unique_id()
  -> ha_entity_upsert()
  -> ha_state_set()
```

`tests/ha_core_semantic_path.c` uses only protocol/vendor-neutral data and proves duplicate semantic observations upsert the same Device/Entities instead of growing the pools.

B/C may later add the thinnest glue needed to translate their already-matched output into these direct calls. A must not absorb their scanner, parser or recognition structures.

## Stable Agent E read/interaction contract

Agent E should use:

- `ha_device_count()` / `ha_device_at()`;
- `ha_entity_count_for_device()` / `ha_entity_at_for_device()` for a selected Device;
- `ha_state_get()`;
- `ha_state_set_listener()` for State publish/update notification;
- `ha_core_revision()` to detect structural/state mutation across an enumeration pass;
- `ha_entity_supports_service()` / `ha_entity_call_service()` for existing HA service names.

Returned pointers are borrowed from fixed pools and are invalid for caching across successful mutations. E should keep `entity_id`/Device id values if it needs durable keys, then re-resolve pointers.

No UI-specific ViewModel is introduced.

## Boundary/stress coverage added

The host tests now exercise:

- Device/Entity/State pool exhaustion using a deliberately tiny-capacity build;
- too many attributes;
- no-identity Device rejection;
- identifier collision;
- connection collision;
- missing Device reference from Entity;
- invalid HA-style Entity ID;
- duplicate `domain + platform + unique_id`;
- attempted Entity identity retarget;
- disabled/unavailable service gating;
- unterminated input rejection;
- cascade cleanup and repeated reset;
- generic semantic re-upsert stability.

## Explicit exclusions

No BLE/Wi-Fi/Zigbee/Thread/Matter packet parsing, vendor decoder, discovery matcher, recognition DB, HA discovery database extraction, config-flow runtime, persistence, recorder/history, areas, automations, scenes, templates, generic event bus, DeviceGraph, Capability, Action, ViewModel, Provider or Adapter framework belongs in this component.

## Provenance

Home Assistant Core is Apache-2.0. This MCU component currently reuses HA names and audited behavior but does not copy Python source bodies.

Audit references at `2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48`:

- `homeassistant/helpers/device_registry.py`
- `homeassistant/helpers/entity_registry.py`
- `homeassistant/helpers/entity.py`
- `homeassistant/core.py`
- first-pass domain modules for `sensor`, `binary_sensor`, `switch`, `light`, `button`
