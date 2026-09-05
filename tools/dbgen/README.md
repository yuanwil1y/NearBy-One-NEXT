# NearBy DB generator PoC

This is a PC-side, stdlib-only proof of concept. It extracts upstream static knowledge without importing Home Assistant/ZHA runtimes and writes a versioned `.nbdb` file with the 128-byte pre-format validation header.

## Inputs

- Home Assistant: point `--ha-bluetooth` at `homeassistant/generated/bluetooth.py` from a pinned HA Core checkout.
- ZHA: point `--zha-root` at the `zhaquirks/` directory from a pinned `zigpy/zha-device-handlers` checkout. v0 extracts literal `MODELS_INFO` pairs and simple `QuirkBuilder` / `applies_to` pairs; decoder/custom cluster code remains firmware-side.
- OUI: point `--oui-csv` at an IEEE-style CSV supplied at build time. Bulk OUI redistribution is intentionally marked `review-required` until terms are cleared.

Always pass immutable upstream revisions in release/reproducible builds.

```bash
python tools/dbgen/nearby_dbgen.py build \
  --ha-bluetooth ../core/homeassistant/generated/bluetooth.py --ha-rev <ha-commit> \
  --zha-root ../zha-device-handlers/zhaquirks --zha-rev <zha-commit> \
  --oui-csv ./oui.csv --oui-rev <registry-date-or-hash> \
  --db-version 1 --out build/nearby-test.nbdb

python tools/dbgen/nearby_dbgen.py validate build/nearby-test.nbdb
python tools/dbgen/nearby_dbgen.py lookup build/nearby-test.nbdb 'oui:001A2B'
```

`validate --release` additionally fails if any source manifest entry is not `redistribution=allowed`; therefore an OUI-inclusive PoC is structurally valid but intentionally not release-cleared by default.

## Bounded lookup

The payload stores a fixed-size sorted index followed by key/value bytes. A lookup binary-searches by seeking to one 16-byte index entry and then the candidate key bytes. It never loads the whole DB into RAM. The values are canonical JSON in the PoC so the extractor and validation contract are easy to inspect; the format benchmark decides the production payload representation separately.
