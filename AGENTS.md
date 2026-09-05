# Agent C — Recognition database / ecosystem knowledge

## Mission
Build the read-only recognition database and generator pipeline used to identify nearby devices without runtime persistence.

## Non-negotiable rules
- SD card stores static recognition data only; runtime Device/Entity/State stays RAM-only.
- Prefer upstream datasets and generated metadata over hand-authored recognition rules.
- Preserve provenance and license metadata for every imported source.
- Do not create a plugin VM, dynamic parser language, or general scripting runtime on ESP32.
- Classify sources as `DATA_ONLY`, `DATA+PARSER`, or `CODE_ONLY`.
- Complex parsers/codecs stay compiled in firmware; SD holds searchable cold data.

## Priority sources
1. Home Assistant discovery matchers/generated data.
2. ZHA quirks / `zigpy/zha-device-handlers`.
3. Zigbee2MQTT `zigbee-herdsman-converters`.
4. HA integration product/parser tables such as Xiaomi BLE, SwitchBot, Shelly.
5. IEEE OUI, Bluetooth SIG assigned numbers, Matter VID/PID, Zigbee manufacturer/model/profile IDs.
6. ESPHome Devices and Theengs only where useful and license-compatible with the intended distribution.

## Own
- Source coverage inventory and license audit.
- PC-side generator that converts upstream knowledge into NearBy DB artifacts.
- Read-only SD DB format, indexes, string dedup/sharding, version/schema/build metadata.
- Fast lookup APIs keyed by protocol-specific matcher keys.
- Database header, magic, schema version, database version, size/integrity metadata.
- Import validation contract used by Web Portal before destructive SD formatting/upload.
- DB version/status information exposed to Agent E.

## Do not own
- Runtime Device/Entity semantics (A).
- Scanner/parsers (B), except determining whether a source requires compiled parser support.
- SD hardware/HTTP upload transport (D).
- UI/Web visual design (E).

## First deliverables
1. Produce the source/licensing/coverage matrix and `THIRD_PARTY.md` conventions.
2. Build a small generator PoC using HA matcher data + at least one Zigbee knowledge source + assigned-number/OUI data.
3. Benchmark SQLite-read-only vs sharded CBOR/MessagePack vs custom sorted flat binary; prefer the simplest format that meets RAM/lookup constraints.
4. Define DB header and validation rules so the browser/device can reject a bad file **before formatting SD**.
5. Define streaming install success criteria and final SD layout.
6. Expose a tiny runtime lookup API that never loads the full DB into RAM.

## Target layout direction
Protocol shards + sorted indexes + deduplicated strings are preferred. Include source provenance/license/version metadata in generated artifacts or companion metadata.

## Definition of done for first pass
A PC generator can build a versioned test DB from real upstream sources; ESP-side code can open it read-only from SD, validate it, query representative BLE/Zigbee/LAN/vendor keys with bounded RAM, and expose DB version/status.