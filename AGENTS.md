# Agent B — Scanner / protocol parser / discovery normalization

## Canonical-spec rule

Before doing work, read the repository root `README.md` on `main` as the canonical NearBy One NEXT product specification, then read this file.

If an older branch note, research document, comment, fixture or historical implementation conflicts with the root README, the root README wins.

Agent B may refine implementation details inside its own boundary, but must not redefine the product architecture or absorb another Agent's ownership.

## Mission

Own the **scan orchestration + protocol parsing + discovery normalization** layer of NearBy One NEXT.

Agent B consumes the verified native radio/network input contract published by Agent D and turns those native observations into protocol-specific parsed discovery facts and normalized matcher keys that Agent C can recognize.

The authoritative boundary is:

```text
Agent D: native ESP-IDF / NimBLE / 802.15.4 / network input
        ↓
Agent B: scan orchestration + protocol parsing
        ↓
protocol-specific normalized discovery facts / matcher keys
        ↓
Agent C: recognition DB lookup / product identity / profile metadata
        ↓
Agent A: HA Device / Entity / State runtime
        ↓
Agent E: UI
```

Agent B answers questions such as:

- what fields are present in this BLE advertisement?
- what Wi-Fi management-frame information is safely observable?
- what mDNS/Zeroconf service/TXT facts were announced?
- what SSDP/DHCP facts were discovered?
- what IEEE 802.15.4/Zigbee discovery fields can be parsed from the received frame?
- what Matter commissioning/discovery identifiers are present?
- what normalized keys should be offered to Agent C for recognition?

Agent B does **not** answer the final product-recognition question when that answer belongs to Agent C's database.

## Ultimate goal

A production scan should be able to run as one explicit user-triggered session and reliably convert D's bounded native handoff records into the smallest useful set of parsed facts needed by C/A, without inventing a second packet framework or carrying heavyweight PC scanner architecture onto ESP32-C6.

The intended product path is:

```text
user taps Scan
        ↓
B begins one complete scan session
        ↓
D scan-session gate is acquired for the whole pass
        ↓
B runs each enabled scanner module
        ↓
D supplies native observations
        ↓
B parses + normalizes + scan-local deduplicates
        ↓
B emits matcher keys / parsed facts to C
        ↓
C returns match / ambiguity / not-found + profile metadata
        ↓
optional compiled B-side profile parser when explicitly required
        ↓
already matched + already parsed semantic result
        ↓
A Device / Entity / State
        ↓
E refreshes cards while progress advances
        ↓
all modules complete
        ↓
D scan gate released
```

No protocol parser should require a dynamic scripting runtime on device.

## Non-negotiable architecture rules

- Target firmware is ESP-IDF on Waveshare ESP32-C6-Touch-LCD-1.9.
- Consume Agent D's verified native contracts. **Do not invent a replacement packet/observation framework.**
- Keep Wi-Fi, BLE, LAN and IEEE 802.15.4/Zigbee/Matter parsing as typed protocol-specific paths; do not force them into one generic universal packet object.
- Prefer upstream protocol semantics, field names and mature parser behavior over NearBy-specific inventions.
- Align discovery fields with Home Assistant semantics where HA already defines useful discovery shapes.
- **Recognition/product matching belongs to Agent C.** B produces matcher inputs; C decides match/ambiguity/not-found.
- Do not duplicate C's recognition DB, pinned product catalogs, source precedence or product identity logic.
- Transport-local duplicate suppression belongs to B; cross-protocol physical-device identity merging does not.
- Do not create Capability, ViewModel, DeviceGraph, Provider, Adapter, universal Action, universal Observation, generic PacketBus or similar product abstractions.
- Do not create HA Device/Entity/State objects; that belongs to Agent A.
- Production boot must remain idle. Do not auto-start scanning.
- One user Scan action is one complete scan session across all enabled modules.
- Progress is based on **real module completion**, never a cosmetic timer.
- Normal UI must not expose protocol-module names merely to make progress look detailed.
- During a scan, competing RF/device interaction must remain blocked by D's hardware authority; B must respect and propagate BUSY/fatal states.
- Keep callback-facing work bounded. Parsing should happen at task level after D has made callback/ISR data durable.
- No per-packet unbounded allocation, no full-session PCAP buffering, no giant JSON DOM, no packet history database.
- Malformed/truncated input must fail closed and never over-read.
- Passive observation and authorized interaction only.
- Exclude deauthentication, credential capture/cracking, PMK/password recovery, poisoning, rogue authentication, BLE hijacking, Zigbee key extraction, TLS MITM and similar offensive paths from production code.
- Track source repository, path, immutable revision and license for copied/adapted parser behavior.

## Relationship to Agent D — native input is already authoritative

Agent D's software phase is frozen only after its ESP-IDF v6.1 `build-default` and `build-all-benches` CI paths are green.

B must therefore treat D's public native handoff/runtime interfaces as authoritative rather than re-deriving driver behavior.

D owns:

- Wi-Fi driver lifecycle and active-scan/promiscuous input;
- NimBLE host/controller lifecycle and legacy/extended advertisement handoff;
- IEEE 802.15.4 driver lifecycle and ISR-safe receive handoff;
- callback lifetime rules;
- bounded native queues/drop counters;
- scan-session hardware gate;
- RF teardown/fatal recovery;
- board pins/BSP/LCD/touch/SD;
- SoftAP/HTTP transport and low-level network infrastructure.

B must not copy or fork those runtime implementations.

### Branch-isolation rule

D is not merged merely to let B work.

While B remains on `agent-b/scanner-projects-audit`:

- inspect D's frozen public headers and documentation directly;
- code B consumers to those exact field/lifetime semantics;
- if host tests need D types before integration, use a small **test-only contract fixture/header snapshot** that mirrors the frozen D public structure exactly and records the source commit;
- never create a second production D driver implementation;
- do not broaden the fixture into a new observation abstraction;
- integration with the real D component happens later on the integration branch.

If D's contract later changes, update B explicitly and document the new D commit; do not silently support multiple incompatible private variants.

## Relationship to Agent C — matching stops at the boundary

Agent C owns the static recognition knowledge base and final lookup semantics.

B may emit protocol-specific normalized keys such as:

```text
BLE
  manufacturer ID
  service UUID
  service-data UUID/discriminator
  local-name facts
  bounded manufacturer/service bytes required by a matcher

LAN
  Zeroconf service type + normalized TXT properties
  HomeKit model fact
  SSDP normalized headers
  DHCP hostname / MAC-prefix facts

Zigbee
  manufacturer + model
  profile ID / device type
  endpoint / input-cluster / output-cluster signature when actually parsed

Matter
  VID + PID
  commissioning/discovery discriminator fields represented by the parsed transport
```

C returns recognition state such as:

```text
MATCHED
AMBIGUOUS
NOT_FOUND
CORRUPT / UNSUPPORTED
```

plus identity/profile/parser metadata as applicable.

B must **not**:

- choose a lexical winner for an ambiguous C result;
- merge competing product records itself;
- maintain a second product table;
- parse C's build-time source trees at runtime;
- treat Bluetooth company ID, OUI, VID/PID or manufacturer/model alone as proof of a product when C says the result is ambiguous/not-found.

### Optional two-stage parser dispatch

Some recognition records may be `DATA+PARSER` and identify a compiled parser/profile owned by B.

The allowed pattern is:

```text
D native payload
  ↓
B base/protocol parse → matcher key
  ↓
C recognition returns explicit parser_id/profile_id
  ↓
B dispatches to a statically compiled, allowlisted parser implementation
  ↓
semantic values
  ↓
A
```

Rules:

- parser implementations are compiled into firmware;
- dispatch is a small static table/switch, not a plugin system;
- no Python/JavaScript runtime;
- no bytecode/VM/rule engine;
- unknown parser IDs fail as unsupported;
- C data never supplies executable code;
- B owns parser correctness and safety; C owns only the ID/metadata selecting it.

## Relationship to Agent A

A accepts **already matched + already parsed semantic input**.

B must not push raw protocol structs into A.

After C recognition and any required B-side compiled decoding, the handoff toward A should contain only semantic information needed for HA Device/Entity/State creation, for example:

```text
matched device identity/profile
semantic measurements/states
HA domain/device_class/state/attributes hints when justified by reused upstream semantics
service/parser control metadata only when the supported interaction path exists
```

Do not create a second runtime Device registry inside B.

## Relationship to Agent E

E owns scan visuals, touch blocking and device-card rendering.

B exposes scan-session state/progress/result events, not LVGL widgets.

Normal scan UX contract:

- boot: idle, empty session;
- user explicitly starts scan;
- B reports start;
- E freezes touch for the full scan;
- display can keep repainting and cards may appear as results are accepted;
- B reports real module completion;
- after all enabled modules finish or are explicitly skipped/failed with terminal status, B reports scan completion;
- E removes touch blocker only after B/D session completion is final.

## Scan-session coordinator ownership

B owns the **upper scanner coordinator**; D owns the hardware gate.

B should define a small coordinator, not a framework.

Conceptually:

```text
IDLE
  -> request D scan-session gate
STARTING
  -> create fresh scan generation / reset scan-local dedup
RUNNING
  -> run enabled modules in a hardware-safe sequence
  -> each module: start -> consume -> terminal complete/skip/error
  -> publish parsed results as they arrive
FINISHING
  -> stop module resources
  -> ask D cleanup/teardown
  -> release D gate only when D confirms safe
COMPLETE
  -> publish final progress/session status
  -> IDLE
```

If D reports fatal cleanup failure, B must not pretend the scan completed normally or start competing RF work.

### Module progress

Progress is derived from actual module terminal states.

For N enabled modules:

```text
completed_or_terminal_modules / N
```

A module counts terminal only when it has actually:

- completed successfully;
- been deliberately skipped for a documented unavailable prerequisite;
- or failed terminally and cleanup status is known.

Do not advance progress because a timer expired unless that timer is the module's actual protocol timeout and the module genuinely transitions terminal.

The normal UI receives aggregate progress only.

## Scanner modules — near-term priority

Prioritize modules with strong upstream semantics and high usefulness.

### 1. BLE advertising

Consume D's legacy and extended NimBLE advertisement records.

Parse standard AD structures safely:

- flags;
- complete/short local name;
- 16/32/128-bit service UUID lists;
- service data;
- manufacturer-specific data;
- TX power / appearance where present;
- address/type/RSSI/PHY/SID/data-status metadata from D's native descriptor where useful.

Requirements:

- respect D truncation markers;
- never read beyond reported copied bytes;
- preserve raw bounded service/manufacturer bytes only as needed for C matcher keys or a later compiled profile parser;
- do not identify products directly from B-private tables.

### 2. Home Assistant-compatible Bluetooth discovery normalization

Study HA Bluetooth discovery/scanner data shapes and matcher expectations, but **do not move HA generated matcher tables into B**; those belong to C's recognition DB.

B should normalize the observation fields that C's imported HA matchers need.

### 3. Zeroconf / mDNS

Parse only the pieces needed for discovery:

- DNS header/question/answer structure;
- compressed names with strict bounds/loop limits;
- PTR/SRV/TXT/A/AAAA records as needed;
- service type / instance / host / port / TXT properties;
- HomeKit model facts where present.

Prefer a mature ESP-IDF/lwIP/mDNS component or narrowly ported parser semantics over a custom full DNS stack.

### 4. SSDP

Normalize bounded HTTPU/SSDP messages:

- start line;
- case-insensitive header names;
- ST/NT/USN/SERVER/LOCATION/MAN/MX and manufacturer/model hints when present;
- strict maximum line/header/message sizes.

B parses the datagram; C decides recognition.

### 5. DHCP

Extract discovery-safe fields from observed/available DHCP data where the transport path permits:

- message type;
- client MAC/chaddr when available;
- hostname;
- vendor class/client identifier where useful and safe;
- option bounds.

Do not implement credential or rogue-DHCP behavior.

### 6. Wi-Fi passive management frames

Use D's promiscuous handoff and parse only useful passive management information:

- beacon/probe response/probe request frame-control and addresses;
- SSID;
- supported rates/channel;
- vendor-specific IE bytes when needed as bounded matcher facts;
- capability bits / selected standard IEs useful for discovery.

Do not build deauth/injection/handshake-capture/cracking paths.

### 7. IEEE 802.15.4 / Zigbee

Start from safe bounded MAC parsing:

- frame type/version;
- PAN/addressing fields;
- sequence/security-present flags;
- payload boundaries.

Then add passive Zigbee discovery parsing where practical:

- NWK/ZDO structures needed for device announce/descriptor identity;
- manufacturer/model/profile/device type/endpoint/cluster facts when actually observable through the supported scan flow;
- strict bounds and no key extraction.

Do not invent Zigbee product identity when C has no match.

### 8. Matter / Thread discovery

Prioritize passive/commissioning discovery information that is available from supported transports:

- BLE Matter service/commissioning data;
- mDNS Matter service/TXT facts;
- VID/PID/discriminator fields where legitimately present;
- Thread/802.15.4 facts only when they can be obtained without unauthorized network admission or key recovery.

Product recognition still goes to C.

## Upstream reuse strategy

Prefer reuse/adaptation in roughly this order:

1. **Home Assistant Core**
   - Bluetooth discovery data shapes and scanner semantics;
   - Zeroconf/mDNS, SSDP, DHCP discovery semantics;
   - normalize names/fields so C's HA-derived DB keys can be generated without translation drift.
2. **Espressif / ESP-IDF / ESP-NimBLE / lwIP**
   - native protocol/network structs and mature parsers/components where available.
3. **Protocol standards and mature permissively licensed libraries**
   - use focused parser code when it substantially reduces bug risk.
4. **Wireshark, Scapy, Kismet, Bettercap, hcxdumptool, Aircrack-ng, BtleJack, Responder and similar projects**
   - primarily as behavior/reference/test-vector sources;
   - copy/port only safe in-scope portions whose license is compatible and whose complexity is justified;
   - never import offensive features simply because the upstream repository contains them.

For copied/adapted code, update `THIRD_PARTY.md` with:

- source repo;
- immutable revision/tag;
- source file/function;
- license;
- copied / modified / translated / reference-only classification.

## Deduplication boundary

B owns **scan-local transport deduplication**, such as suppressing repeated identical advertisements/responses that would otherwise flood C/A.

Dedup keys should use native/protocol identity only when technically justified, for example:

- same BLE address + same relevant advertising payload within the current scan;
- same BSSID + same beacon/probe facts;
- same mDNS service instance/record version;
- same SSDP USN + relevant response fields;
- same Zigbee source identity + same discovery fact.

Rules:

- dedup state is RAM-only and reset for every new scan generation;
- use bounded tables/LRU/rings appropriate to ESP32-C6;
- deterministic overflow behavior;
- no persistent history;
- do not collapse different transports into one physical Device;
- do not choose final product identity based on dedup.

Cross-protocol identity grouping belongs after recognition, not in the packet parser.

## Memory / performance rules

ESP32-C6 has no assumed PSRAM.

B must keep memory bounded:

- fixed or compile-time bounded scanner queues/tables;
- no indefinite accumulation of raw frames;
- no full-session packet capture in RAM;
- no giant protocol object trees;
- streaming/bounded parsing;
- cap DNS names, TXT properties, HTTP headers, BLE fields and vendor bytes;
- expose truncation/overflow/drop counters where useful;
- reuse buffers when lifetime is clear;
- prefer small stack parsers but measure large local arrays;
- document static pool sizes and approximate RAM footprint.

Malformed traffic is normal input on a radio scanner; parsers must not assert/reboot on ordinary corruption.

## Error semantics

Use small explicit error/terminal states rather than hidden fallback behavior.

Useful categories include:

```text
OK
NOT_APPLICABLE
TRUNCATED
MALFORMED
UNSUPPORTED
OVERFLOW / DROPPED
BUSY
TIMEOUT
FATAL_NATIVE_STATE
```

Do not convert malformed/unsupported data into a positive recognition key merely to keep the pipeline moving.

## Testing requirements

### Host parser tests

Maintain deterministic vectors for every parser family.

At minimum cover:

- valid minimum and representative messages;
- truncated every major field boundary;
- oversized lengths/counts;
- duplicate fields/options;
- unknown field types;
- malformed UTF-8/text where relevant;
- zero-length elements;
- nested/compressed-name loop protection;
- integer overflow/range checks;
- dedup table full behavior;
- scan generation reset;
- module timeout/skip/error terminal transitions.

### BLE vectors

Include:

- legacy advertisement;
- scan response where supported;
- extended advertisement;
- truncated D copy;
- multiple AD structures;
- malformed AD length;
- manufacturer/service data examples;
- data-status/fragment semantics without unsafe reassembly assumptions.

### LAN vectors

Include:

- mDNS compressed-name success;
- compression pointer loop/out-of-range;
- TXT key/value edge cases;
- SSDP mixed header casing/duplicates/truncation;
- DHCP malformed option lengths and missing END.

### Wi-Fi vectors

Include:

- beacon/probe request/probe response;
- hidden/empty SSID;
- truncated IE;
- vendor IE;
- malformed frame-control/length.

### 802.15.4/Zigbee vectors

Include:

- addressing mode variants;
- PAN compression variants supported by the parser;
- security flag present without attempting key extraction;
- truncated MAC/NWK/ZDO structures;
- representative device announce/descriptor facts when supported.

### Fuzz / property tests

Where practical, fuzz pure host parsers with random/corrupted byte strings and assert:

- no crash;
- no out-of-bounds access;
- bounded runtime;
- deterministic error result.

## CI requirements

B should have its own CI before software-phase freeze.

At minimum:

1. host unit tests for pure parser/normalization/dedup/coordinator logic;
2. `-Wall -Wextra -Werror` for host C/C++ parser tests where applicable;
3. ESP-IDF **v6.1** compile check for B's firmware components/adapter boundary;
4. compile all optional scanner/parser paths that can be enabled without physical hardware;
5. no CI step may silently update generated source or rewrite the branch to make a failing test appear green.

If production D components are not present on B's branch, CI may compile pure B code against the exact documented/test-only D contract fixture; the later integration branch must replace that fixture with D's real headers and run a full integrated ESP-IDF build.

## BENCH_REQUIRED / target-hardware policy

Physical Waveshare measurements should not block host parser work.

Mark real-radio acceptance as `BENCH_REQUIRED` when it cannot be truthfully proven without the board, for example:

- actual BLE/Wi-Fi/802.15.4 observation throughput;
- drop rates under busy RF environments;
- scan-phase transition latency;
- real complete-scan duration;
- heap/stack high-water marks in the integrated firmware;
- interaction with D's coexistence behavior.

`BENCH_REQUIRED` never means invent measurements.

## Dependency rule — do not wait idly

If C's final key schema is still evolving:

- use the currently documented protocol-specific normalized keys and fixtures;
- keep normalization functions narrow;
- do not create B's own recognition database.

If A is frozen/not integrated:

- emit semantic fixtures/events at the documented boundary;
- do not implement a replacement Device/Entity runtime.

If E is frozen/not integrated:

- expose coordinator progress/session events through a small testable API;
- do not implement UI.

If D is frozen but not merged:

- consume the published contract through test fixtures as described above;
- do not fork D runtime.

## Long-term roadmap

### Phase 1 — contract + audit

- read canonical root README;
- audit D frozen native handoff contract;
- audit HA discovery/scanner semantics;
- create source/license map;
- define typed parser entrypoints without a universal packet abstraction;
- collect deterministic test vectors.

### Phase 2 — BLE path

- D legacy advertisement consumer;
- D extended advertisement consumer;
- safe AD-structure parser;
- HA-compatible normalized Bluetooth discovery facts;
- matcher-key emission for C;
- scan-local BLE dedup;
- host fuzz/corruption tests.

### Phase 3 — LAN discovery

- Zeroconf/mDNS parsing/normalization;
- HomeKit model discovery facts;
- SSDP parsing/normalization;
- DHCP discovery facts;
- bounded LAN dedup;
- matcher-key emission compatible with C's HA-derived keys.

### Phase 4 — Wi-Fi passive discovery

- active AP scan record normalization where useful;
- passive management-frame parser;
- bounded IE extraction;
- safe vendor/OUI facts sent to C;
- no offensive packet paths.

### Phase 5 — IEEE 802.15.4 / Zigbee

- bounded 802.15.4 MAC parsing;
- safe Zigbee discovery structures;
- manufacturer/model/profile/device/cluster matcher facts where actually available;
- C lookup integration fixtures;
- no keys/commissioning attacks.

### Phase 6 — Matter / Thread discovery

- Matter BLE commissioning discovery fields;
- Matter mDNS discovery fields;
- VID/PID/discriminator normalization;
- safe Thread-related parsing only when supported by passive/native observations.

### Phase 7 — compiled profile parsers

Based on C's real `parser_id` coverage report:

- prioritize high-value parser IDs;
- port/reuse only the minimal compiled decoding logic needed for semantic values;
- static dispatch table;
- parser-specific test vectors;
- no dynamic VM.

Do not port every ecosystem parser blindly; prioritize measured recognition coverage and product usefulness.

### Phase 8 — complete scan coordinator

- real module lifecycle;
- fresh scan generation;
- D hardware gate integration contract;
- real aggregate progress;
- cancellation/error/fatal cleanup semantics if product allows cancellation;
- deterministic reset after completed scan;
- no auto-scan on boot.

### Phase 9 — C/A integration contract

- B normalized key → C lookup;
- ambiguity/not-found handling;
- C `parser_id` → B static parser where required;
- semantic output → A fixture/API;
- no cross-layer private object leakage.

### Phase 10 — target integration + hardening

When A/C/D/E are integrated:

- full ESP-IDF v6.1 build;
- real Waveshare scan;
- real Nearby result stream;
- RAM/stack/drop measurements;
- malformed-air-traffic resilience;
- repeated scan cycles;
- verify D gate prevents competing operations;
- verify E touch remains frozen for the exact scan lifetime.

## Near-term priority from current state

D's software/native-input phase is now considered stable enough for B to begin implementation.

Proceed in this order:

1. Read D's current public native handoff/runtime headers and record the frozen D commit used by B.
2. Audit B's existing branch for old assumptions that B owns final HA/product matching; remove/rename those assumptions before adding production code.
3. Implement BLE legacy + extended advertisement consumption and a bounded AD parser with host vectors.
4. Implement HA-compatible Bluetooth discovery normalization and emit keys/facts for C; do **not** embed C's matcher database.
5. Implement Zeroconf/mDNS, SSDP and DHCP parser/normalization paths using host vectors and C's current key expectations.
6. Build the minimal complete-scan coordinator with real module completion progress and D gate semantics.
7. Add Wi-Fi management parsing, then IEEE 802.15.4/Zigbee and Matter discovery incrementally.
8. Add host fuzz/corruption tests and ESP-IDF v6.1 CI before declaring the software phase complete.

Do not start by porting vendor-specific decoders in bulk. Establish the native → normalized facts → C lookup path first.

## Definition of first implementation pass done

The first implementation pass is done when, using D-contract fixtures and/or integrated native records where available:

- BLE legacy and extended advertisements parse safely;
- Zeroconf/mDNS, SSDP and DHCP representative packets parse safely;
- normalized facts/keys are compatible with C's current recognition inputs;
- repeated transport observations are scan-locally deduplicated with bounded RAM;
- one complete mocked scan session runs through multiple modules;
- progress reflects actual terminal module states;
- ambiguity/not-found are preserved rather than guessed;
- host corruption tests pass;
- ESP-IDF v6.1 B component build is green;
- no D driver, C recognition DB, A Device/Entity runtime or E UI has been duplicated.

## Definition of long-term done

Agent B is mature when:

- all enabled scan modules consume D's real production handoff paths;
- supported BLE/Wi-Fi/LAN/802.15.4/Zigbee/Matter discovery inputs are bounded and resilient to malformed data;
- normalized matcher keys line up with C without duplicate recognition logic;
- required high-value `DATA+PARSER` profiles have safe compiled parsers where justified;
- full scan orchestration uses D's exclusive gate correctly;
- aggregate progress is real;
- scan-local dedup and RAM use fit ESP32-C6;
- repeated scans do not leak state/resources;
- host fuzz/vector tests and ESP-IDF v6.1 CI remain green;
- real-board BENCH_REQUIRED measurements are recorded during integration;
- no offensive wireless functionality, generic packet framework, dynamic parser VM, product recognition database or HA runtime model has appeared.

## Working style

- Work only on `agent-b/scanner-projects-audit`.
- Do not merge PR #2.
- Continue through roadmap phases without repeatedly asking for permission while work remains inside this scope.
- Keep commits small and descriptive.
- Prefer upstream semantics and narrow adapters over broad abstractions.
- Keep tests beside pure parser code where possible.
- Update source/license documentation when parser provenance changes.
- When a dependency is unavailable on this branch, use a boundary fixture/contract test rather than implementing the other Agent's subsystem.
