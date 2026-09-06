# NearBy recognition DB generators

Agent C tooling is PC-side and deliberately static: it converts pinned upstream recognition knowledge into a read-only `.nbdb` container. It never imports Home Assistant/ZHA runtimes, never executes Zigbee2MQTT/zigbee-herdsman/SmartThings code, and never parses radio/network packets.

## Frozen pinned proof

`pins.json` is the source of truth for the release-proof revisions. The frozen Agent C pipeline currently covers:

- Home Assistant generated Bluetooth matchers;
- Home Assistant generated HomeKit model matchers plus Zeroconf, SSDP and DHCP matchers;
- ZHA literal manufacturer/model + simple `QuirkBuilder`/`applies_to` identifiers;
- Zigbee2MQTT literal `zigbeeModel`, literal manufacturer/model fingerprints and common `tuya.fingerprint(...)` literal arguments;
- Nordic Semiconductor `bluetooth-numbers-database` company IDs and service UUIDs from a pinned BSD-3-Clause revision;
- Koenkk `zigbee-herdsman` static manufacturer codes plus HA/Smart Energy/Green Power/Touchlink profile IDs from a pinned MIT revision;
- SmartThings Edge Drivers literal `matterManufacturer` VID/PID fingerprints from a pinned Apache-2.0 revision.

The Matter source is an integration fingerprint catalog, not the authoritative CSA assignment registry. Records retain that distinction in `catalog_semantics`.

No additional source/protocol expansion is part of the frozen Agent C branch. Direct live/gated source experiments remain out of the release proof.

## Reproducibility verification

`.github/workflows/agent-c-pinned-build.yml` is verification-only. It has read-only repository permissions, fetches the immutable pinned upstream inputs, regenerates DB/report files under `/tmp`, runs release preflight, and compares the regenerated results with the committed proof artifacts. It never commits or pushes generated files.

The DB comparison is byte-for-byte and SHA-256 based. The report comparison uses a deterministic canonical projection. Legacy `timing_ms` is ignored because host wall-clock timing is not reproducibility metadata, and `db.path` is normalized to the logical release path. New `build_pinned.py` reports no longer emit timing and always record the logical DB path, so future manual refreshes are deterministic.

## Manual artifact refresh only

Updating the committed proof artifacts is an explicit maintainer action, not a CI side effect. From already-fetched pinned trees/files, run the generator with the committed output paths, release-preflight the DB, inspect the diff/hashes, then commit those two files manually.

```bash
python tools/dbgen/build_pinned.py \
  --pins tools/dbgen/pins.json \
  --ha-bluetooth ../core/homeassistant/generated/bluetooth.py \
  --ha-zeroconf ../core/homeassistant/generated/zeroconf.py \
  --ha-ssdp ../core/homeassistant/generated/ssdp.py \
  --ha-dhcp ../core/homeassistant/generated/dhcp.py \
  --zha-root ../zha-device-handlers/zhaquirks \
  --z2m-root ../zigbee-herdsman-converters/src/devices \
  --bt-company-ids ../bluetooth-numbers-database/v1/company_ids.json \
  --bt-service-uuids ../bluetooth-numbers-database/v1/service_uuids.json \
  --zigbee-manufacturer-codes ../zigbee-herdsman/src/zspec/zcl/definition/manufacturerCode.ts \
  --zigbee-consts ../zigbee-herdsman/src/zspec/consts.ts \
  --matter-root ../SmartThingsEdgeDrivers \
  --out artifacts/pinned/nearby.nbdb \
  --report docs/db/coverage-pinned.json

python tools/dbgen/preflight.py artifacts/pinned/nearby.nbdb --release
sha256sum artifacts/pinned/nearby.nbdb docs/db/coverage-pinned.json
git diff -- artifacts/pinned/nearby.nbdb docs/db/coverage-pinned.json
```

The repository proof artifacts are `artifacts/pinned/nearby.nbdb` and `docs/db/coverage-pinned.json`. The deterministic report records immutable pins, raw claims and runtime-key counts separately, protocol/record-type coverage, unresolved static-extraction gaps, ambiguity/conflict provenance, DB SHA-256/size, and representative lookup status.

## Collision policy

Different claims for one normalized key are never silently treated as resolved. Canonical JSON ordering is only a reproducibility mechanism. An unresolved collision is stored as `recognition_ambiguity` with every distinct candidate and its provenance; the flat index carries ambiguity flag `0x0001`. The C reader returns `NEARBY_DB_AMBIGUOUS`, not `NEARBY_DB_OK`, while preserving a value reference so candidates can be inspected. There is no lexical product winner. Identical duplicate records collapse without creating a conflict. See `docs/db/conflict-semantics.md`.

## Bounded lookup and target benchmark gate

The payload stores a fixed-size sorted index followed by key/value bytes. A lookup binary-searches by seeking one 16-byte index entry at a time and then the candidate key bytes; it does not load the full DB into RAM. The ESP-IDF-style `components/recognition_db` reader follows the same seek/read contract.

Performance status is **`TARGET_BENCH_REQUIRED`**. The existing host benchmark is only a relative format comparison; it is not evidence for ESP32-C6+SD latency. `flat-v0` and schema v1 remain provisional and are not locked until the reader is benchmarked through the real SD/FAT path on the Waveshare ESP32-C6-Touch-LCD-1.9. See `docs/db/format-benchmark.md`.
