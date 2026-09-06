# Recognition DB format benchmark v0

**Status: `TARGET_BENCH_REQUIRED`**

The current `flat-v0` container and schema v1 are **provisional and not locked**. Host measurements justify continuing with the bounded sorted-flat direction, but they are not sufficient to freeze the production index/schema contract. The freeze gate is a real SD lookup benchmark on the actual **Waveshare ESP32-C6-Touch-LCD-1.9** using the board's real FAT/SD path.

Repro command for the existing host-only comparison:

```bash
python tools/dbgen/benchmark_formats.py --records 50000 --lookups 3000
```

The benchmark uses 50,000 deterministic representative BLE/Zigbee/OUI/mDNS key/value records and 3,000 deterministic successful lookups per round. Numbers below were produced by CPython on the development host; they are **relative format measurements, not ESP32-C6 latency predictions**.

| Candidate | File bytes | Median host lookup | Lookup memory / IO shape | ESP32-C6 assessment |
|---|---:|---:|---|---|
| SQLite read-only (`WITHOUT ROWID`, 4 KiB pages, cache capped to 64 KiB) | 8,126,464 | 4.89 us | B-tree pages + SQLite page cache; also requires SQLite library/runtime | Excellent PC ergonomics, but highest code/runtime complexity for a tiny firmware reader. |
| 256 sharded MessagePack (representative of CBOR/MessagePack class) | 7,706,217 | 57.48 us | Read + decode entire selected shard; largest shard 36,191 B in this run | Smallest file, but shard decode creates a RAM floor above the preferred 16 KiB install/lookup buffer unless shard count grows. More files/shards also increase FAT/SD metadata IO. |
| Custom sorted flat binary | 8,330,481 | 22.23 us | Binary-search 16-byte fixed index entries, seek/read only candidate key/value | Slightly larger file but simplest bounded-RAM reader and no general DB/decoder dependency. |

## Current direction, not a locked schema

Continue using **custom sorted flat binary** for the current proof artifact because it keeps the firmware reader bounded and simple. This is a working direction only:

- `flat-v0` is not a permanent on-device ABI promise;
- schema v1 is not frozen;
- current generic key/index layout may change after target measurements;
- no additional protocol/data-source expansion is justified merely to tune the format before the target benchmark exists.

Why the direction remains reasonable for the proof artifact:

- 1 GB SD capacity makes the ~2.5% size penalty versus SQLite and ~8% versus MessagePack irrelevant at this scale.
- The flat reader needs only fixed-size index reads plus the matched key/value; it never needs to deserialize a whole shard or maintain a DB page cache.
- Firmware implementation can remain a small C module using bounded `seek/read` operations plus the existing integrity checks.
- SQLite remains useful as an optional **PC-side build/intermediate format**, not the on-device distribution format.
- CBOR/MessagePack remain useful for build tooling/metadata where whole-object decode is acceptable, but not as the primary random-lookup store unless target measurements overturn this direction.

## `TARGET_BENCH_REQUIRED` acceptance work

Before locking schema v1 or declaring `flat-v0` stable, repeat lookup measurements on the actual Waveshare ESP32-C6-Touch-LCD-1.9 with a real SD card through Agent D's production SD/FAT access path and collect:

- DB open/header-check time;
- cold/warm lookup p50/p95/p99;
- SD read/seek count and bytes per representative lookup;
- peak heap delta and stack usage where practical;
- fragmented FAT vs freshly formatted FAT behavior;
- representative current DB size plus 100k/500k/1M scaling if practical;
- protocol-specific lookup keys/index shapes, not only a generic string key.

Until those measurements exist, performance status remains exactly **`TARGET_BENCH_REQUIRED`**. Host benchmark results must not be presented as ESP32-C6+SD performance evidence, and they must not be used to claim schema v1 or flat-v0 is locked.

The benchmark source and raw host JSON result remain checked in only as a relative comparison baseline for the later target run.
