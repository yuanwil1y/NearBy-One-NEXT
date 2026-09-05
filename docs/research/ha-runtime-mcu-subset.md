# Home Assistant runtime semantics -> ESP32-C6 MCU subset

Audit pin: `home-assistant/core@9b312180c69890fcad3d0adea76ccfd46b339544` (`dev`, 2026-09-06).

Goal: preserve the useful HA runtime contract `Integration -> Platform -> Device + Entity + State -> UI`, while deleting persistence, recorder/history, Python runtime machinery and other server-only behavior.

## Source map

| HA concern | upstream source | MCU treatment |
|---|---|---|
| Device registry / `DeviceInfo` / identifiers / connections | `homeassistant/helpers/device_registry.py` | fixed-size `ha_device_t`; RAM-only upsert/find/remove |
| Entity registry identity and display fields | `homeassistant/helpers/entity_registry.py` | fixed-size `ha_entity_t`; `domain + platform + unique_id`; device attachment |
| Entity runtime behavior | `homeassistant/helpers/entity.py` | only availability/enabled flags and direct service callback needed for first pass |
| State object / state machine | `homeassistant/core.py` (`State`) | `entity_id + state + attributes`; one lightweight state-update listener |
| Integration/config entry lifecycle | integration `__init__.py`, `config_entries`, platform forwarding | scanner/parser integration owns setup/teardown; config entry/subentry ids are retained as association strings, persistence/config-flow machinery omitted |
| Platform entity creation | integration domain platform modules (`sensor.py`, `switch.py`, etc.) | platform/parser calls `ha_device_upsert`, `ha_entity_upsert`, then `ha_state_set` |
| Domain semantics | `components/sensor`, `binary_sensor`, `switch`, `light`, `button` | first five HA domains only; add future types only by existing HA domain name |

## Device field subset

HA's current `BaseDeviceEntry`/`DeviceEntry` includes one `config_entry_id`, optional `config_subentry_id`, identifiers/connections, metadata and persistence/migration fields. v0.1 keeps only fields useful during one boot session.

| HA field / behavior | MCU | reason |
|---|---|---|
| `id` | copied | opaque runtime Device key used by attached Entities |
| `config_entry_id` | copied | current HA ownership model is one config entry per device |
| `config_subentry_id` | copied | preserves current HA association where an integration uses subentries |
| `identifiers` | copied, max 4 | primary registry identity input; exposed lookup helper |
| `connections` | copied, max 4 | MAC/Bluetooth/etc. identity input; exposed lookup helper |
| `manufacturer` | copied | UI/device info |
| `model` | copied | UI/device info |
| `model_id` | copied | HA product identity field |
| `name` | copied | UI display |
| `serial_number` | copied | useful identity/diagnostic field |
| `sw_version` | copied | useful UI/diagnostic field |
| `hw_version` | copied | useful UI/diagnostic field |
| `configuration_url` | copied | optional device management link semantics |
| `via_device_id` | copied | preserves parent-via-device relationship without adding a graph abstraction |
| `area_id`, labels | omitted | areas/rooms and persistent organization explicitly out of scope |
| `name_by_user` | omitted | no persistent user registry customization in RAM-only v0.1 |
| `created_at`, `modified_at` | omitted | no persistence/history requirement |
| `disabled_by` | omitted on Device | first pass only needs Entity enabled/available gating |
| `entry_type` | omitted | service-device distinction not needed for nearby device UI first pass |
| composite migration fields | omitted | HA storage migration compatibility only |
| suggested area / pending move | omitted | deprecated/transient HA server registry machinery |
| registry storage/migration/orphan cleanup | omitted | boot starts empty; no SQLite/storage registry |

Device upsert is keyed by caller-supplied `id`; integrations should first resolve existing Devices by HA-style identifiers/connections. v0.1 intentionally does not implement HA's full collision/merge/migration algorithm or proprietary cross-protocol identity fusion.

## Entity field subset

Current HA `RegistryEntry` includes many storage/UI customization fields. The MCU keeps identity, Device association and display/domain semantics needed by integrations and UI.

| HA field / behavior | MCU | reason |
|---|---|---|
| `entity_id` | copied | public UI/state key, including HA domain prefix |
| `unique_id` | copied | integration-provided stable identity |
| `platform` | copied | HA registry identity dimension |
| `domain` | copied | UI/domain behavior selector |
| `device_id` | copied | Device -> Entities ownership |
| `config_entry_id` / `config_subentry_id` | copied | lifecycle ownership/alignment with Device |
| `device_class` | copied | UI semantics such as temperature/motion |
| `entity_category` | copied | supports future diagnostic/config presentation without a new type system |
| `name` | copied | UI display |
| `icon` | copied | optional HA-style UI metadata |
| `unit_of_measurement` | copied | sensor display |
| `supported_features` | copied | preserves HA feature bitmask field for integrations that need it later |
| `has_entity_name` | copied | HA naming semantics |
| enabled/available runtime flags | retained | cheap first-pass lifecycle/service gating |
| `aliases`, `area_id`, categories, labels | omitted | persistent organization/search metadata out of scope |
| `capabilities` mapping | omitted for v0.1 | do not create a parallel Capability framework; add exact HA domain data only when a real integration needs it |
| entity options/customization fields | omitted | persistent UI/user registry machinery |
| created/modified timestamps | omitted | no registry persistence/history |
| hidden/disabled provenance enums | omitted | server registry customization not required for first pass |
| translation fields | omitted | firmware UI localization can be added separately without carrying HA backend registry storage |

`ha_entity_upsert()` rejects unsupported domains and requires `entity_id` to share the same domain prefix. `ha_entity_get_by_unique_id(domain, platform, unique_id)` provides the HA registry identity lookup shape.

## State subset

HA `State` currently contains `entity_id`, derived domain/object id, `state`, `attributes`, timestamps and context. The MCU keeps the UI contract only.

| HA State field / behavior | MCU | reason |
|---|---|---|
| `entity_id` | copied | primary key |
| `state` | copied as fixed UTF-8 string | HA state is represented to consumers as a string |
| `attributes` | copied as max-8 string key/value pairs | enough for device_class/unit/brightness/etc. without Python `Any` containers |
| derived `domain`, `object_id` | not stored | derivable from `entity_id`; Entity already stores domain |
| `last_changed`, `last_updated`, `last_reported` | omitted | recorder/history/time-series out of scope |
| `context` | omitted | no HA event bus/automation causality in v0.1 |
| full StateMachine event bus | omitted | one direct state listener is enough for LVGL invalidation/update |
| restore-state | omitted | every boot starts empty |

## Integration / Platform lifecycle mapping

The MCU does not create a second framework around HA integrations. An integration/parser module follows a thin lifecycle:

```text
scanner discovery record
  -> integration matcher/parser
  -> find/upsert Device
  -> find/upsert domain Entity/Entities
  -> set State + attributes
  -> UI listener/enumeration

UI service request
  -> ha_entity_call_service(entity_id, HA service name)
  -> integration-owned callback
  -> protocol driver
  -> integration publishes resulting State

integration/device disappears
  -> remove Entity(s) or Device
  -> associated State removed

boot/session restart
  -> ha_core_reset()
```

This maps HA's setup/add-entities/state-write/service direction without porting `asyncio`, config flow, config-entry storage, the full event bus or service registry.

## Public API for Agent B / C / E

Agent B/C integration glue should call:

- `ha_device_get_by_identifier()` / `ha_device_get_by_connection()`
- `ha_device_upsert()` / `ha_device_remove()`
- `ha_entity_get_by_unique_id()` / `ha_entity_upsert()` / `ha_entity_remove()`
- `ha_state_set()`
- interactive Entity `service_handler`

Agent E UI should call:

- `ha_device_count()` / `ha_device_at()`
- `ha_entity_count()` / `ha_entity_at()`
- `ha_state_get()` or `ha_state_count()` / `ha_state_at()`
- `ha_state_set_listener()` for refresh notification
- `ha_entity_call_service()` for switch/light/button interaction
- `ha_mock_fixtures_load()` for deterministic mock UI development

## First-pass exclusions

No recorder, history, statistics, restore-state, areas/rooms, automations, scenes, templates, persistent dashboards, SQLite registry, Python runtime, generic event bus, cross-protocol DeviceGraph, Capability/Action/ViewModel/Provider abstraction, or proprietary domain names.
