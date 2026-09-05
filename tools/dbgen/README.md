# NearBy recognition DB generators

Agent C tooling is PC-side and deliberately static: it converts pinned upstream recognition knowledge into a read-only `.nbdb` container. It never imports Home Assistant/ZHA runtimes, never executes Zigbee2MQTT TypeScript/JavaScript, and never parses radio/network packets.

## Reproducible pinned proof

`pins.json` is the source of truth for the release-proof revisions. The GitHub Actions proof fetches only those immutable revisions and builds:

- Home Assistant generated Bluetooth matchers;
- Home Assistant generated HomeKit model matchers plus Zeroconf, SSDP and DHCP matchers;
- ZHA literal manufacturer/model + simple `QuirkBuilder`/`applies_to` identifiers;
- Zigbee2MQTT literal `zigbeeModel`, literal manufacturer/model fingerprints and common `tuya.fingerprint(...)` literal arguments;
- Nordic Semiconductor `bluetooth-numbers-database` company IDs and service UUIDs from a pinned BSD-3-Clause revision.

Run the same build locally from already-fetched pinned trees/files:

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
  --out build/nearby.nbdb \
  --report build/coverage.json

python tools/dbgen/preflight.py build/nearby.nbdb --release
```

The repository proof artifacts are `artifacts/pinned/nearby.nbdb` and `docs/db/coverage-pinned.json`. The report records immutable pins, raw claims and runtime-key counts separately, protocol/record-type coverage, unresolved static-extraction gaps, ambiguity/conflict provenance, DB SHA-256/size, build/validation timing and representative lookup status.

## Collision policy

Different claims for one normalized key are never silently treated as resolved. Canonical JSON ordering is only a reproducibility mechanism. An unresolved collision is stored as `recognition_ambiguity` with every distinct candidate and its provenance; the flat index carries ambiguity flag `0x0001`. The C reader returns `NEARBY_DB_AMBIGUOUS`, not `NEARBY_DB_OK`, while preserving a value reference so candidates can be inspected. There is no lexical product winner. Identical duplicate records collapse without creating a conflict. See `docs/db/conflict-semantics.md`.

## Focused PoC and OUI input

`nearby_dbgen.py build` remains useful for focused HA Bluetooth/ZHA/OUI experiments. OUI input is an IEEE-style CSV supplied at build time; direct IEEE bulk OUI output remains `review-required` until redistribution terms are explicitly cleared, so `validate --release` rejects an OUI-inclusive PoC by default. Prefer a separately audited permissive mirror/source before adding OUI data to the release pinned proof.

## Bounded lookup

The payload stores a fixed-size sorted index followed by key/value bytes. A lookup binary-searches by seeking one 16-byte index entry at a time and then the candidate key bytes; it does not load the full DB into RAM. The ESP-IDF-style `components/recognition_db` reader follows the same seek/read contract. Values remain canonical JSON in the current PoC while format/reader benchmarks continue separately.
