# Recognition source / license / coverage matrix v0

Status legend: **COPY** = mechanically transform/copy into generated DB with notices; **BUILD-READ** = generator may read an external checkout/download but distributable bulk output is gated; **REFERENCE** = use for coverage research/tests only until project licensing policy changes.

| Priority | Source | Recognition value | Typical keys | Class | License / terms | v0 action |
|---:|---|---|---|---|---|---|
| 1 | Home Assistant Core generated discovery tables | Maps BLE, Zeroconf, SSDP, DHCP and other observations to integration domains | BLE manufacturer ID, service UUID/data UUID, local-name pattern; mDNS service/TXT; SSDP headers | DATA_ONLY for generated matchers; DATA+PARSER where integration decoder is required | Apache-2.0 | **COPY**. PoC extracts `homeassistant/generated/bluetooth.py` via Python AST without importing HA. |
| 2 | ZHA quirks / zha-device-handlers | Zigbee manufacturer/model fingerprints, endpoint/cluster signatures and replacement metadata | manufacturer + model, endpoints, profile/device type, clusters | DATA+PARSER | Apache-2.0 | **COPY** declarative identifiers. Complex custom clusters/transforms stay firmware code. |
| 3 | Zigbee2MQTT zigbee-herdsman-converters | Large Zigbee product/fingerprint catalog and exposes metadata | `zigbeeModel`, fingerprints, vendor/model, endpoint mapping | DATA+PARSER | MIT | **COPY** normalized product/fingerprint metadata; JS converters are not executed on device. |
| 4 | HA integration requirement tables (Xiaomi BLE, SwitchBot, Shelly, etc.) | Vendor-specific product IDs and decoder selection | manufacturer/service data, model IDs, product IDs | DATA+PARSER or CODE_ONLY | Per dependency | Audit per dependency before import; static tables can become DB records, decoder logic remains compiled. |
| 5 | IEEE OUI registry | MAC prefix -> organization enrichment and DHCP/LAN hints | 24/28/36-bit OUI assignments | DATA_ONLY | IEEE registry terms, not treated here as a permissive OSS license | **BUILD-READ**. PoC accepts official CSV as an input and marks output `review-required`. Do not vendor the bulk list yet. |
| 5 | Bluetooth SIG Assigned Numbers | Company identifiers, UUIDs, appearances | company ID, assigned UUID, appearance | DATA_ONLY | Bluetooth SIG terms | **BUILD-READ** pending redistribution review. |
| 5 | Matter vendor/product identifiers | VID/PID naming | VID/PID | DATA_ONLY | CSA / source-specific terms | **BUILD-READ** pending source selection and redistribution review. |
| 5 | Zigbee assigned manufacturer/profile IDs | Manufacturer/profile name enrichment | manufacturer code, profile ID | DATA_ONLY | Source-specific | Prefer permissively licensed generated/constants source; keep provenance. |
| 6 | ESPHome Devices | Product aliases, board/chipset and catalog enrichment | manufacturer/model/aliases | DATA_ONLY enrichment | Repository GPL-3.0-or-later | **REFERENCE** by default; bulk redistribution requires explicit project policy. |
| 6 | Theengs Decoder | Excellent BLE product/decoder coverage | manufacturer/service payload fingerprints | DATA+PARSER | GPL-family | **REFERENCE** by default; useful as a test oracle and coverage map. |

## Coverage contract

Each normalized record carries `source_id`, `source_revision`, `classification`, and `parser_id` (empty for `DATA_ONLY`). Build reports aggregate:

- record count per source/protocol;
- unique matcher-key count;
- conflicting normalized keys and selected winner;
- records requiring parser IDs not present in the firmware capability manifest;
- sources whose redistribution state is not `allowed`.

A release build MUST fail closed if a `review-required` or `reference-only` source is requested for distributable embedding without an explicit allowlist entry.

## v0 scope

The first executable PoC intentionally covers three different knowledge shapes:

1. HA generated Bluetooth matcher dictionaries;
2. ZHA literal manufacturer/model pairs plus simple `QuirkBuilder("manufacturer", "model")` declarations;
3. IEEE-style OUI CSV rows supplied at build time.

This is enough to prove normalization, provenance, sorting, deduplication, DB header generation and pre-format validation without committing to a dynamic parser language.
