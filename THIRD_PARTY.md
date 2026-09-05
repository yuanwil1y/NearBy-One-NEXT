# Third-party recognition data conventions

NearBy One NEXT builds a read-only recognition database from upstream metadata. Runtime Device/Entity/State remains RAM-only; this file governs only copied or mechanically transformed recognition data.

## Rules

Every imported source MUST have a source record with:

- `source_id`: stable lowercase identifier used in generated records.
- `name` and `upstream_url`.
- `upstream_revision`: immutable commit/tag/hash used for the build.
- `license_spdx`: SPDX identifier when known; otherwise `LicenseRef-*`.
- `license_url` and a vendored notice/license path when redistribution requires it.
- `classification`: `DATA_ONLY`, `DATA+PARSER`, or `CODE_ONLY`.
- `redistribution`: `allowed`, `review-required`, or `reference-only`.
- `transform`: exact generator/extractor name and whether the output is mechanical or curated.

A generated database MUST retain the source IDs used by each record and MUST emit a build manifest containing the complete source table. Do not strip provenance to save bytes; deduplicate source IDs instead.

## Redistribution policy

1. Apache-2.0 / MIT / BSD-family sources may be mechanically transformed when required notices are preserved.
2. Datasets with terms that are not a standard open-source license stay `review-required` until redistribution rights are explicitly confirmed.
3. GPL-family sources are not copied into distributable artifacts by default. They may be used as a reference/test oracle unless the project intentionally adopts compatible distribution terms.
4. Parser/code dependencies are tracked separately from data. A data record may be redistributable while the decoder still requires compiled firmware code.
5. Generator output is reproducible from pinned upstream revisions; hand-authored recognition entries are an exception and must identify a maintainer and rationale.

## Initial source IDs

| source_id | Upstream | License / terms | Class | Redistribution |
|---|---|---|---|---|
| `ha_core` | Home Assistant Core | Apache-2.0 | DATA_ONLY + DATA+PARSER by integration | allowed |
| `zha_quirks` | zigpy/zha-device-handlers | Apache-2.0 | DATA+PARSER | allowed |
| `z2m_converters` | Koenkk/zigbee-herdsman-converters | MIT | DATA+PARSER | allowed |
| `bluetooth_numbers_nordic` | Nordic Semiconductor bluetooth-numbers-database mirror | BSD-3-Clause terms in repository `LICENSE`; mirror tracks Bluetooth assigned-number metadata | DATA_ONLY | allowed with notice |
| `ieee_oui` | IEEE public OUI registry | LicenseRef-IEEE-OUI-Terms | DATA_ONLY | review-required |
| `bluetooth_sig_assigned` | Bluetooth SIG official Assigned Numbers publication | LicenseRef-Bluetooth-SIG-Terms | DATA_ONLY | review-required as a direct bulk source |
| `matter_ids` | Matter / CSA assigned vendor-product metadata | LicenseRef-CSA-Terms | DATA_ONLY | review-required |
| `esphome_devices` | ESPHome Devices catalog | GPL-3.0-or-later repository content; dataset redistribution requires project policy | DATA_ONLY enrichment | reference-only by default |
| `theengs_decoder` | Theengs Decoder | GPL-3.0-family project | DATA+PARSER | reference-only by default |

The Nordic mirror is intentionally tracked separately from the official Bluetooth SIG source: the generated artifact cites the redistributable mirror revision and license rather than treating publication on the SIG website as an automatic redistribution grant.

This is an engineering inventory, not legal advice. Any `review-required` source must be cleared before its bulk data is shipped in a NearBy release image.
