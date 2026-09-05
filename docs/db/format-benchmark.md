# Recognition DB format benchmark v0

Repro command:

```bash
python tools/dbgen/benchmark_formats.py --records 50000 --lookups 3000
```

The benchmark uses 50,000 deterministic representative BLE/Zigbee/OUI/mDNS key/value records and 3,000 deterministic successful lookups per round. Numbers below were produced by CPython on the development host; they are **relative format measurements, not ESP32-C6 latency predictions**.

| Candidate | File bytes | Median host lookup | Lookup memory / IO shape | ESP32-C6 assessment |
|---|---:|---:|---|---|
| SQLite read-only (`WITHOUT ROWID`, 4 KiB pages, cache capped to 64 KiB) | 8,126,464 | 4.89 us | B-tree pages + SQLite page cache; also requires SQLite library/runtime | Excellent PC ergonomics, but highest code/runtime complexity for a tiny firmware reader. |
| 256 sharded MessagePack (representative of CBOR/MessagePack class) | 7,706,217 | 57.48 us | Read + decode entire selected shard; largest shard 36,191 B in this run | Smallest file, but shard decode creates a RAM floor above the preferred 16 KiB install/lookup buffer unless shard count grows. More files/shards also increase FAT/SD metadata IO. |
| Custom sorted flat binary | 8,330,481 | 22.23 us | Binary-search 16-byte fixed index entries, seek/read only candidate key/value | Slightly larger file but simplest bounded-RAM reader and no general DB/decoder dependency. |

## Decision

Use **custom sorted flat binary** as the production direction for v0.

Why:

- 1 GB SD capacity makes the ~2.5% size penalty versus SQLite and ~8% versus MessagePack irrelevant at this scale.
- The flat reader needs only fixed-size index reads plus the matched key/value; it never needs to deserialize a whole shard or maintain a DB page cache.
- Firmware implementation can be a small C module using `fseek`/`fread` (or ESP-IDF VFS equivalents), CRC/SHA verification, and protocol-specific sorted indexes.
- SQLite remains useful as an optional **PC-side build/intermediate format**, not the on-device distribution format.
- CBOR/MessagePack remain useful for build tooling/metadata where whole-object decode is acceptable, but not as the primary random-lookup store.

## Follow-up benchmark requirements on target hardware

Before locking schema v1, repeat with the actual Waveshare ESP32-C6-Touch-LCD-1.9 + chosen SD card and collect:

- cold/warm lookup p50/p95/p99;
- SD read count and bytes per representative lookup;
- heap delta/peak during lookup;
- boot/open/validate time;
- fragmented FAT vs freshly formatted FAT behavior;
- 100k/500k/1M record scaling;
- protocol-specific indexes (manufacturer ID, UUID, OUI prefix, Zigbee manufacturer+model), not only a generic string key.

The benchmark source and raw JSON result are checked in so later runs can be compared rather than replacing conclusions by intuition.
