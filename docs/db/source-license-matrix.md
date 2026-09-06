# Recognition source / license / coverage matrix v0

Status legend: **COPY** = mechanically transform/copy into generated DB with notices; **BUILD-READ** = generator may read an external checkout/download but distributable bulk output is gated; **REFERENCE** = use for coverage research/tests only until project licensing policy changes.

| Priority | Source | Recognition value | Typical keys | Class | License / terms | v0 action |
|---:|---|---|---|---|---|---|
| 1 | Home Assistant Core generated discovery tables | Maps BLE, Zeroconf, SSDP, DHCP and other observations to integration domains | BLE manufacturer ID, service UUID/data UUID, local-name pattern; mDNS service/TXT; SSDP headers | DATA_ONLY for generated matchers; DATA+PARSER where integration decoder is required | Apache-2.0 | **COPY** pinned generated matcher tables without importing HA runtime. |
| 2 | ZHA quirks / zha-device-handlers | Zigbee manufacturer/model fingerprints, endpoint/cluster signatures and replacement metadata | manufacturer + model, endpoints, profile/device type, clusters | DATA+PARSER | Apache-2.0 | **COPY** declarative identifiers. Complex custom clusters/transforms stay firmware code. |
| 3 | Zigbee2MQTT zigbee-herdsman-converters | Large Zigbee product/fingerprint catalog and exposes metadata | `zigbeeModel`, fingerprints, vendor/model, endpoint mapping | DATA+PARSER | MIT | **COPY** normalized product/fingerprint metadata; JS converters are not executed on device. |
| 4 | HA integration requirement tables (Xiaomi BLE, SwitchBot, Shelly, etc.) | Vendor-specific product IDs and decoder selection | manufacturer/service data, model IDs, product IDs | DATA+PARSER or CODE_ONLY | Per dependency | Audit per dependency before import; static tables can become DB records, decoder logic remains compiled. |
| 5 | Nordic Semiconductor bluetooth-numbers-database | Redistributable mirror of Bluetooth assigned-number metadata | company ID, service UUID | DATA_ONLY | BSD-3-Clause repository license | **COPY** pinned `company_ids.json` + `service_uuids.json`; preserve source revision/notice. |
| 5 | Koenkk zigbee-herdsman static definitions | Redistributable Zigbee manufacturer-code mirror plus core public profile constants | manufacturer code, HA/SE/GP/Touchlink profile ID | DATA_ONLY | MIT | **COPY** pinned `ManufacturerCode` enum and explicit public profile constants only; do not import adapter/ZCL runtime. |
| 5 | SmartThings Edge Drivers Matter fingerprints | Known manufacturer-specific Matter devices used by SmartThings matching | VID + PID, device label/profile | DATA_ONLY | Apache-2.0 | **COPY** pinned `matterManufacturer` literal fields. Mark as integration fingerprint catalog, not CSA authoritative assignment registry. |
| 5 | Bluetooth SIG official Assigned Numbers | Authoritative assigned numbers | company ID, assigned UUID, appearance | DATA_ONLY | Bluetooth SIG publication terms | **BUILD-READ** / reference unless direct bulk redistribution is explicitly cleared. Do not conflate with the Nordic mirror license. |
| 5 | IEEE OUI public listings | MAC prefix -> organization enrichment and DHCP/LAN hints | 24/28/36-bit assignments | DATA_ONLY | IEEE states the public listing is not copyright-restricted; Debian `ieee-data` records the listing as Public Domain | Candidate **COPY** only after pinning an integrity-stable package/version + license evidence in the release pipeline; direct ad-hoc live downloads remain gated. |
| 5 | CSA/DCL authoritative Matter vendor/product identifiers | Authoritative VID/PID assignment/certification metadata | VID/PID | DATA_ONLY | CSA/DCL terms | **BUILD-READ** until the bulk dataset's redistribution terms are independently established. Public read access alone is insufficient. |
| 6 | ESPHome Devices | Product aliases, board/chipset and catalog enrichment | manufacturer/model/aliases | DATA_ONLY enrichment | Repository GPL-3.0-or-later | **REFERENCE** by default; bulk redistribution requires explicit project policy. |
| 6 | Theengs Decoder | Excellent BLE product/decoder coverage | manufacturer/service payload fingerprints | DATA+PARSER | GPL-family | **REFERENCE** by default; useful as a test oracle and coverage map. |

## Coverage contract

Each normalized source claim carries `source_id`, `source_revision` and classification/provenance fields. Build reports keep two concepts separate:

- **raw source claims**: all mechanically extracted claims before cross-source/key deduplication;
- **runtime keys**: final indexed keys after identical dedupe and ambiguity wrapping.

Reports also include protocol/record-type runtime counts, conflict/ambiguity counts, parser-required claim counts, redistribution gates and output size/hash/timing.

Unresolved key collisions do **not** have a selected product winner. They become `recognition_ambiguity` records with complete candidates/provenance and an ambiguity index flag. Canonical JSON sorting is for reproducible candidate order only. See `conflict-semantics.md`.

A release build MUST fail closed if a `review-required` or `reference-only` source is requested for distributable embedding without an explicit policy change/allowlist.

## Current pinned release-proof scope

The real pinned proof covers:

1. HA generated Bluetooth, HomeKit-model, Zeroconf, SSDP and DHCP matchers;
2. ZHA literal manufacturer/model and simple declarative applies-to identifiers;
3. Zigbee2MQTT static `zigbeeModel`, literal fingerprints and common literal `tuya.fingerprint(...)` arguments;
4. Nordic BSD-3-Clause Bluetooth company IDs and service UUIDs;
5. MIT zigbee-herdsman manufacturer codes plus HA/Smart Energy/Green Power/Touchlink profile IDs;
6. Apache-2.0 SmartThings Matter manufacturer VID/PID fingerprints, explicitly labeled non-authoritative catalog data.

For OUI, Debian's `ieee-data` copyright metadata provides useful redistribution evidence: it records IEEE's OUI public listing as Public Domain and says IEEE does not restrict distribution. The release pipeline should still pin a concrete package/version and preserve this evidence before changing the existing direct-IEEE PoC from `review-required` to an allowed source.

CSA DCL remains gated until an independently verified bulk redistribution contract is established; the SmartThings catalog fills useful VID/PID recognition coverage without pretending to replace the authoritative registry.
