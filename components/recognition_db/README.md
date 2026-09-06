# recognition_db component

Minimal read-only ESP-IDF-facing reader for the Agent C `.nbdb` PoC format.

Properties:

- validates magic/schema/reader ABI/header CRC and container ranges at open;
- validates the flat payload header/index bounds;
- binary-searches 16-byte index entries with a 64-byte comparison scratch buffer;
- keeps `nearby_db_find()` for exact C-owned keys;
- exposes typed B -> C BLE/Zeroconf/SSDP/DHCP matcher queries so B supplies normalized facts, not C-owned domain/index/digest keys;
- uses the existing sorted key anchors as the matcher index and reads only the small candidate ranges from SD;
- evaluates the stored HA matcher metadata inside C with caller-supplied bounded scratch memory (`NEARBY_DB_MATCH_WORKSPACE_RECOMMENDED` is 4096 bytes for the current pinned DB);
- returns `NEARBY_DB_MATCHED`, `NEARBY_DB_MATCH_AMBIGUOUS`, or `NEARBY_DB_MATCH_NOT_FOUND`; multiple matching records for the same HA domain remain one match, while different domains or an ambiguous DB entry fail closed as `AMBIGUOUS`;
- returns a `(value_offset, value_size)` reference only for a unique match and supports chunked value reads;
- never loads the full DB, matcher table, full index, shard, or candidate set into RAM;
- does not parse BLE advertisements, mDNS, SSDP, DHCP packets, HA Device/Entity state, or execute vendor code.

## B -> C normalized fact contract

B passes already parsed facts through `nearby_db_match_ble()`, `nearby_db_match_zeroconf()`, `nearby_db_match_ssdp()`, or `nearby_db_match_dhcp()`. UUID comparisons are ASCII case-insensitive. BLE local-name patterns follow the stored HA glob semantics. Zeroconf name/property and DHCP hostname/MAC patterns are matched case-insensitively for ASCII normalization. SSDP field names are case-insensitive and field values are exact, matching HA's generated integration matcher behavior; `"*"` means the field must be present.

The query code does not expose or require the generated HA `domain`, matcher index number, or matcher digest. Those remain Agent C implementation details. The current implementation reuses `flat-v0` sorted key prefixes as its bounded candidate index; this does **not** lock `flat-v0` or schema v1. Target ESP32-C6 + real SD performance remains `TARGET_BENCH_REQUIRED`.

`registered_devices` DHCP matchers are only considered when `nearby_db_dhcp_facts_t.registered_device` is explicitly true. A B-only network observation should leave it false; C never fabricates HA registry state.

Full payload CRC32/SHA-256 verification remains an install-time responsibility before a staged DB generation is promoted. Runtime `open()` intentionally stays cheap.
