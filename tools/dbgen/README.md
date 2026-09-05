# NearBy recognition DB generators

Agent C tooling is PC-side and deliberately static: it converts pinned upstream recognition knowledge into a read-only `.nbdb` container. It never imports Home Assistant/ZHA runtimes, never executes Zigbee2MQTT TypeScript/JavaScript, and never parses radio/network packets.

## Reproducible pinned proof

`pins.json` is the source of truth for the release-proof revisions. The GitHub Actions proof fetches only those immutable revisions and builds:

- Home Assistant generated Bluetooth matchers;
- Home Assistant generated Zeroconf, SSDP and DHCP matchers;
- ZHA literal manufacturer/model + simple `QuirkBuilder`/`applies_to` identifiers;
- Zigbee2MQTT literal `zigbeeModel`, literal manufacturer/model fingerprints and common `tuya.fingerprint(...)` literal arguments.

Run the same build locally from already-fetched pinned trees:

```bash
python tools/dbgen/build_pinned.py \
  --pins tools/dbgen/pins.json \
  --ha-bluetooth ../core/homeassistant/generated/bluetooth.py \
  --ha-zeroconf ../core/homeassistant/generated/zeroconf.py \
  --ha-ssdp ../core/homeassistant/generated/ssdp.py \
  --ha-dhcp ../core/homeassistant/generated/dhcp.py \
  --zha-root ../zha-device-handlers/zhaquirks \
  --z2m-root ../zigbee-herdsman-converters/src/devices \
  --out build/nearby.nbdb \
  --report build/coverage.json

python tools/dbgen/preflight.py build/nearby.nbdb --release
```

The repository proof artifacts are `artifacts/pinned/nearby.nbdb` and `docs/db/coverage-pinned.json`. The report records immutable pins, raw and unique counts, source/protocol/record-type coverage, unresolved static-extraction gaps, conflicts, DB SHA-256/size, build/validation timing and representative lookups.

## Collision policy

Different claims for one normalized key are never silently treated as resolved. The build keeps all differing candidates in `conflict_ledger`, records their sources, selects a deterministic canonical-JSON lexical winner only so runtime lookup stays deterministic, records that exact reason, and marks the collision `manual_review=true`. Identical duplicate records collapse without creating a conflict.

## Legacy focused PoC and OUI input

`nearby_dbgen.py build` remains useful for focused HA Bluetooth/ZHA/OUI experiments. OUI input is an IEEE-style CSV supplied at build time; bulk OUI output is `review-required` until redistribution terms are cleared, so `validate --release` rejects an OUI-inclusive PoC by default.

```bash
python tools/dbgen/nearby_dbgen.py build \
  --ha-bluetooth ../core/homeassistant/generated/bluetooth.py --ha-rev <ha-commit> \
  --zha-root ../zha-device-handlers/zhaquirks --zha-rev <zha-commit> \
  --oui-csv ./oui.csv --oui-rev <registry-date-or-hash> \
  --db-version 1 --out build/nearby-test.nbdb
```

## Bounded lookup

The payload stores a fixed-size sorted index followed by key/value bytes. A lookup binary-searches by seeking one 16-byte index entry at a time and then the candidate key bytes; it does not load the full DB into RAM. The ESP-IDF-style `components/recognition_db` reader follows the same seek/read contract. Values remain canonical JSON in the current PoC while format/reader benchmarks continue separately.
