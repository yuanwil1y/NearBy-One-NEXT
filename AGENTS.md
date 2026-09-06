# Agent C — Recognition database / ecosystem knowledge

## Mission

Own the **static recognition knowledge** of NearBy One NEXT.

Agent C turns upstream device/discovery knowledge into a compact, versioned, read-only database that lives on the SD card and can be queried with bounded RAM on ESP32-C6.

The intended boundary is:

```text
Agent D: native radio/network input
        ↓
Agent B: scan + protocol parsing
        ↓
normalized observation / matcher keys
        ↓
Agent C: recognition DB lookup
        ↓
matched identity/profile/metadata
        ↓
Agent A: HA Device / Entity / State
        ↓
Agent E: UI
```

Agent C does **not** parse raw packets and does **not** create Home Assistant Devices or Entities. It answers recognition questions from already-parsed matcher keys.

## Ultimate goal

The 1 GB SD card should contain a reproducibly generated knowledge base large enough to recognize as many nearby products as practical without embedding huge tables in firmware or loading the database into RAM.

Adding or updating device knowledge should normally mean:

```text
update/pin upstream datasets
        ↓
run PC-side generator
        ↓
produce nearby.nbdb
        ↓
validate
        ↓
install to SD
```

It should **not** normally require firmware changes unless a source requires new compiled parsing/codec behavior owned by Agent B.

The final runtime contract should stay small: given protocol-specific normalized lookup keys, return matching profile/product/integration metadata and provenance using bounded reads from SD.

## Non-negotiable architecture rules

- SD stores **static recognition/matching data only**. Runtime Device/Entity/State stays RAM-only and belongs to Agent A.
- Prefer upstream datasets and mechanically generated metadata over hand-authored recognition rules.
- Preserve source provenance, pinned revision and license/redistribution status for every imported source.
- Classify source material as `DATA_ONLY`, `DATA+PARSER`, or `CODE_ONLY`.
- `DATA_ONLY` may become DB records when licensing permits.
- `DATA+PARSER` may contribute declarative/static matching metadata; executable decoding logic stays compiled in firmware under Agent B.
- `CODE_ONLY` must not be converted into a dynamic on-device scripting system.
- **Do not create a plugin VM, bytecode format, dynamic parser language, JavaScript/Python runtime, reflection system or general rule engine on ESP32.**
- Do not duplicate Home Assistant Device/Entity/State semantics in the DB.
- Do not invent a NearBy capability/type system.
- Do not load the full DB, a full large shard, a huge JSON tree or a general-purpose database cache into ESP32 RAM.
- Prefer simple sorted indexes, protocol shards, deduplicated strings and bounded `seek/read` access.
- The DB is read-only during normal runtime. Updates happen only through the explicit Web Management import flow.

## Product install rule — whole SD is destructive

The product decision is fixed for v0.1:

**Importing a recognition database formats/reinitializes the entire SD card. All existing SD contents are deleted.**

The install model is therefore:

```text
browser selects .nbdb
        ↓
browser validates the complete local source file
(header + compatibility + integrity)
        ↓
user explicitly confirms destructive whole-SD format
        ↓
Agent D formats/reinitializes SD
        ↓
browser streams .nbdb
        ↓
ESP32 writes directly to SD with bounded buffer
        ↓
CRC/SHA/integrity verification
        ↓
rename/promote completed file as active DB
```

Consequences:

- Do **not** design around preserving the previous DB generation through formatting.
- Do **not** require `/current` generation pointers or multi-generation rollback storage for v0.1.
- Do **not** claim the old DB survives a failed post-format upload.
- Browser-side complete pre-validation is important because formatting is irreversible.
- After format, a failed/cancelled upload may leave no usable DB; the UI should report this clearly, but recovery semantics belong to the next import attempt rather than a generation manager.
- A temporary `.part` file followed by rename after successful verification is fine within the newly formatted SD.

Recommended simple layout:

```text
/nearby/db/
  nearby.nbdb
  # optional nearby.nbdb.part only during import
```

Agent C defines validation and file semantics. Agent D owns the actual FAT/SD formatting, HTTP upload stream and low-level I/O. Agent E owns confirmation/progress visuals.

## Ownership

Agent C owns:

- recognition source inventory;
- source licensing/provenance audit;
- pinned upstream revision policy;
- PC-side generator/extractors;
- normalized static recognition record schema;
- conflict/deduplication policy;
- DB container/header/magic/schema/reader ABI/version metadata;
- protocol-specific indexes and shard layout;
- string/table deduplication where useful;
- build manifest and coverage report;
- release gating for redistribution status;
- runtime read-only lookup API;
- pre-format validation contract;
- stream-install integrity success criteria;
- DB version/status metadata consumed by Agent E;
- target-hardware DB lookup benchmark design, once Agent D exposes stable SD access.

## Explicitly not owned

### Agent D owns

- SD SPI/FATFS mounting and formatting;
- LCD/SD shared-bus hardware behavior;
- HTTP server and upload transport;
- SoftAP/APSTA/Wi-Fi provisioning;
- upload buffers and actual write loop;
- board/radio drivers.

Agent C may provide validation/reader functions callable by D, but must not absorb the Web server or board driver.

### Agent B owns

- BLE advertisement decoding;
- Wi-Fi frame parsing;
- mDNS/SSDP/DHCP parsing;
- IEEE 802.15.4/Zigbee parsing;
- Matter discovery parsing;
- vendor binary payload decoders;
- CRC/crypto/bitfield/state-machine parser code needed to turn raw observations into semantic matcher keys/values.

Agent C may store `parser_id`/profile hints that tell the system which compiled parser is required, but **does not implement or execute that parser**.

### Agent A owns

- Home Assistant Device/Entity/State runtime;
- mapping already-matched/parsed semantic data into HA entities;
- state/service lifecycle.

Agent C returns identity/profile metadata; it must not create Device/Entity registries or HA-specific runtime objects.

### Agent E owns

- LVGL UI;
- Web Portal visuals/forms;
- database import warnings/progress presentation.

Agent C only exposes DB version/status and validation results.

## Priority knowledge sources

Continue coverage in roughly this order, subject to licensing and actual scanner support:

1. **Home Assistant Core generated discovery matchers**
   - Bluetooth
   - Zeroconf/mDNS
   - SSDP
   - DHCP
   - other generated discovery tables useful to NearBy
2. **ZHA quirks / `zigpy/zha-device-handlers`**
   - manufacturer/model pairs
   - declarative endpoint/cluster signatures
   - V2 `QuirkBuilder` declarative applies-to metadata
   - static replacement/entity metadata where useful
3. **Zigbee2MQTT / `zigbee-herdsman-converters`**
   - product/fingerprint/catalog metadata
   - declarative exposes/identity data where licensing and representation are suitable
   - never execute JavaScript converters on-device
4. **HA integration dependency/static product tables**
   - Xiaomi BLE
   - SwitchBot
   - Shelly
   - other integrations with useful static product IDs/fingerprints
   - audit each dependency separately; parser code stays with B
5. **Standards/assigned-number sources**
   - IEEE OUI
   - Bluetooth SIG company IDs/UUIDs/appearances
   - Matter VID/PID metadata
   - Zigbee manufacturer/profile/device identifiers
6. **Enrichment/reference catalogs**
   - ESPHome Devices
   - Theengs Decoder
   - other catalogs only when useful and redistribution-safe

Do not bulk ship a source merely because it is technically accessible. Redistribution status is a release gate.

## Source classification and licensing policy

Every source record must carry at least:

- stable `source_id`;
- upstream project/name;
- immutable commit/tag/hash/date where appropriate;
- SPDX license or `LicenseRef-*`;
- classification: `DATA_ONLY`, `DATA+PARSER`, `CODE_ONLY`;
- redistribution state: `allowed`, `review-required`, `reference-only`;
- extractor/transform provenance.

Rules:

- Apache-2.0/MIT-style permissive data may be transformed with required notices preserved.
- Non-permissive or unclear registry/catalog terms remain `review-required` until explicitly cleared.
- GPL-family repositories are `reference-only` by default unless the overall distribution policy intentionally changes.
- A release DB build must fail closed when a requested source is not permitted for distribution.
- Never strip provenance merely to save a small amount of SD space; deduplicate source IDs/strings instead.
- Keep `THIRD_PARTY.md` and generated manifest data synchronized with generator behavior.

## Runtime lookup contract

Runtime queries originate from normalized observations produced by B, not from raw packets.

Examples of acceptable lookup keys:

```text
bluetooth:manufacturer_id + product/service discriminator
bluetooth:service_uuid/service_data discriminator
zigbee:manufacturer + model
zigbee:profile/device_type/cluster signature
lan:zeroconf service/TXT matcher
lan:ssdp normalized header matcher
lan:dhcp hostname/OUI matcher
vendor:OUI prefix
matter:VID + PID
```

The DB may return fields such as:

- matched integration/profile ID;
- vendor/manufacturer;
- model/model ID;
- product name/aliases;
- device category/class hints;
- parser/profile ID required by compiled firmware;
- static metadata needed by A/B;
- provenance/source ID.

It should not return a proprietary executable action/capability graph.

Lookup API requirements:

- bounded RAM;
- no full DB preload;
- no full large-shard decode;
- explicit not-found/corrupt/unsupported errors;
- cheap runtime open after install-time full integrity verification;
- version/schema/reader ABI exposed;
- deterministic match/conflict precedence generated on the PC side wherever possible.

## DB format direction

The current preferred v0 direction is a **custom sorted flat binary container/index**, because it provides the smallest firmware reader and bounded random-access RAM behavior.

SQLite may remain a PC-side intermediate/build tool. CBOR/MessagePack may remain useful for tooling or small metadata, but neither should become the runtime primary store unless target measurements clearly justify changing direction.

Do not lock schema v1 until target hardware benchmarks validate the index/key design.

The container header must remain independently readable before any destructive SD operation and include enough metadata to reject incompatible/corrupt files without reading arbitrary complex structures first.

## Pre-format validation contract

Before E/D enable destructive formatting, the selected source file must be validated from the browser/local source as fully as practical:

1. minimum size;
2. magic/container version;
3. header size and header CRC;
4. schema version;
5. minimum reader ABI compatibility;
6. declared file size and range/offset consistency;
7. manifest structure/source count;
8. release redistribution policy when applicable;
9. payload CRC32;
10. payload SHA-256 when present/required.

The browser can read the source file in slices/streams; it does not need to buffer the entire DB in memory.

The device repeats cheap compatibility/header checks before accepting the stream and computes payload integrity while writing.

## Generator rules

The generator is a PC-side build tool, not firmware functionality.

It should:

- accept pinned upstream checkouts/files;
- avoid importing heavyweight upstream runtimes when a mechanical AST/data extractor is sufficient;
- normalize source records into deterministic keys/values;
- deduplicate strings/records where practical;
- report conflicts instead of silently discarding them;
- emit deterministic/reproducible output from identical pinned inputs;
- emit source/provenance/license manifest;
- emit per-source/per-protocol coverage counts;
- emit unresolved conflicts and parser requirements;
- fail release builds for blocked redistribution sources;
- emit DB/version/schema/build metadata;
- validate its own output after build.

Hand-authored recognition records should be exceptional and documented with rationale.

## Conflict policy

Recognition sources will disagree. Do not hide that behind arbitrary runtime behavior.

Prefer resolving conflicts during generation using explicit precedence rules and reports.

For each collision, retain enough information to answer:

- which normalized key collided;
- which sources produced it;
- what each candidate claimed;
- which record won and why;
- whether the result still requires manual review.

Runtime lookup should be deterministic and simple.

## Dependency rule: never block waiting for another Agent

Agent C must keep progressing even if B/D are not ready.

If B's final matcher-key schema is not ready:

- use representative normalized fixture keys;
- keep the DB reader generic enough for the documented index form;
- do not invent raw packet parsing.

If D's SD/BSP is not ready:

- keep host reader/generator/integrity tests progressing;
- build a target benchmark harness/API contract that D can later call;
- do not implement a replacement SD driver.

If E's Web Portal backend contract is not ready:

- document exact header/validation/install semantics;
- provide pure validation functions/tooling;
- do not invent E's UI.

## Long-term roadmap

### Phase 1 — source/license inventory and DB PoC

- source coverage matrix;
- `THIRD_PARTY.md` rules;
- initial HA + Zigbee + assigned-number/OUI extractors;
- format benchmark;
- versioned `.nbdb` PoC;
- bounded host/ESP-IDF-style reader;
- corruption tests.

This phase is considered complete only when generator, validator and representative lookup all work.

### Phase 2 — correct destructive install contract

- align docs/tooling with **whole-SD format** product rule;
- remove assumptions that old DB generations survive formatting;
- keep a simple `.part -> nearby.nbdb` post-write promotion if useful;
- define browser pre-validation values/statuses for D/E;
- define streaming CRC/SHA verification contract;
- test truncated/corrupt/incompatible headers and payloads.

### Phase 3 — real pinned upstream build proof

Run the generator against real pinned upstream revisions, not only synthetic fixtures.

At minimum record:

- upstream revisions;
- extraction/build command;
- source record counts;
- normalized unique key counts;
- conflict counts;
- parser-required counts;
- blocked/nonredistributable counts;
- output DB size;
- build/validation time;
- representative successful lookups.

Keep a machine-readable report in the repository so coverage changes can be compared over time.

### Phase 4 — expand Home Assistant discovery coverage

Add extractors/normalizers for useful HA generated discovery knowledge beyond Bluetooth, especially:

- Zeroconf/mDNS;
- SSDP;
- DHCP;
- HomeKit/ESPHome/Matter discovery matcher metadata where represented through HA generated data.

Reuse HA matcher semantics/field names where practical rather than inventing new matching rules.

### Phase 5 — expand Zigbee ecosystem coverage

- improve ZHA declarative signature extraction;
- ingest Zigbee2MQTT declarative product/fingerprint metadata;
- normalize compatible manufacturer/model/profile/cluster keys;
- explicitly mark entries that require compiled B-side parser/custom cluster behavior;
- never embed/execute Python or JS converters on ESP32.

### Phase 6 — standards and vendor metadata coverage

Add or improve:

- Bluetooth SIG metadata;
- Matter VID/PID;
- Zigbee assigned identifiers;
- OUI/vendor metadata subject to redistribution policy;
- static tables from useful HA integration dependencies where licensing permits.

### Phase 7 — DB schema/index hardening

Using real coverage data:

- design protocol-specific index keys rather than one generic demonstration string key;
- add string deduplication where it materially saves space/IO;
- shard only when it improves target lookup/RAM behavior;
- test 100k/500k/1M-record scaling;
- fuzz/corruption-test header/index/range handling;
- preserve backwards/forwards reader ABI rules explicitly.

### Phase 8 — target ESP32-C6 benchmark with Agent D

Once D has stable SD access, measure on the Waveshare ESP32-C6-Touch-LCD-1.9:

- DB open/validate time;
- lookup p50/p95/p99;
- reads/seeks/bytes per lookup;
- peak heap delta;
- stack usage where practical;
- cold vs warm FAT/SD behavior;
- fragmented vs freshly formatted SD;
- representative protocol-specific indexes;
- 100k/500k/1M scaling if build sizes permit.

Use measurements to tune schema/indexes. Do not replace bounded/simple design with speculative complexity.

### Phase 9 — stable contracts for B/A/E/D

Provide small stable interfaces:

**To B:** lookup key expectations and parser/profile metadata returned after matching.

**To A:** already-matched identity/product/profile metadata only; no Device/Entity objects.

**To D:** header validation, stream-integrity and runtime reader contract.

**To E:** DB version/status/error strings/data fields, not UI widgets.

Keep these interfaces independent of the PC generator implementation.

### Phase 10 — reproducible release database pipeline

A mature pipeline should be able to:

```text
fetch/pin approved upstream sources
        ↓
extract + normalize
        ↓
license/provenance gate
        ↓
conflict report
        ↓
build deterministic nearby.nbdb
        ↓
validate/fuzz/smoke test
        ↓
emit coverage + build manifest
        ↓
release artifact for Web Management import
```

No manual database editing should be required for normal releases.

## Tests expected to grow continuously

Maintain tests for:

- deterministic build output;
- valid header parsing;
- bad magic;
- incompatible schema/reader ABI;
- truncated file;
- bad header CRC;
- bad payload CRC/SHA;
- malformed/out-of-range offsets;
- sorted-index lookup hit/miss;
- longest/shortest keys and values;
- chunked value reads;
- duplicate/conflicting normalized keys;
- release license gating;
- real-upstream extractor fixtures/snapshots;
- large synthetic scaling;
- fuzzed/corrupt index entries;
- no unbounded memory allocation in firmware reader paths.

Host tooling tests and ESP-IDF reader tests should validate the same format contract.

## Working style

- Work continuously within `agent-c/device-database`; do not wait for permission between roadmap phases when the next task remains inside this scope.
- Keep commits small and descriptive.
- Update format/source/license documentation whenever the actual generator/reader contract changes.
- Prefer extending mechanical extractors over hand-curating device records.
- Do not merge the PR.
- Do not absorb another Agent's work because their implementation is incomplete; use fixtures/contracts at the boundary.
- When deciding between a broad framework and a narrow indexed-data solution, choose the narrow solution.

## Near-term priority from current state

1. Correct the existing install documentation/model to the **whole-SD destructive format** rule and remove old-generation preservation assumptions.
2. Strengthen header/payload/install corruption tests around that model.
3. Run and record at least one build against real pinned Home Assistant Core + ZHA upstream revisions and emit a coverage/build report.
4. Expand HA discovery extraction beyond Bluetooth, prioritizing Zeroconf, SSDP and DHCP.
5. Add Zigbee2MQTT static recognition coverage without executing JS converters.
6. Continue assigned-number/vendor metadata work subject to redistribution review.
7. Keep the runtime reader bounded and prepare target benchmark hooks for D.

Do not implement scanner/parser code, Device/Entity runtime code, SD drivers, HTTP server code or UI while doing these tasks.

## Definition of long-term done

Agent C is mature when:

- a reproducible PC pipeline builds `nearby.nbdb` from pinned approved upstream sources;
- source provenance and redistribution status are complete and release-gated;
- HA discovery, Zigbee ecosystem and major assigned-number/product metadata have useful coverage;
- the DB can be fully validated before destructive SD formatting;
- the device can stream-install and later query the DB with bounded RAM;
- target ESP32-C6 lookup performance is measured and acceptable;
- B can provide normalized matcher keys without knowing DB internals;
- A receives already-matched identity/profile metadata without knowing recognition datasets;
- E only needs DB version/status and never understands matcher internals;
- adding recognition coverage normally means updating data/generator rules, not firmware architecture;
- no dynamic parser VM, Device/Entity duplication or proprietary capability framework has appeared.
