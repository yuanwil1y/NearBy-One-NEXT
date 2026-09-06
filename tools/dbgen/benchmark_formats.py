#!/usr/bin/env python3
"""Host-side format benchmark for NearBy recognition DB candidates.

This is intentionally a reproducible relative benchmark. It compares on-disk
size and seek/decode lookup cost using the same deterministic records. Host
Python timings are not ESP32-C6 latency predictions; the RAM/IO conclusions are
based on each format's access pattern.
"""
from __future__ import annotations

import argparse
import binascii
import json
from pathlib import Path
import random
import sqlite3
import statistics
import struct
import tempfile
import time

try:
    import msgpack  # type: ignore
except ImportError:
    msgpack = None

ENTRY = struct.Struct("<IHHII")
FH = struct.Struct("<8sIIQQ")
MAGIC = b"NBYFLT1\0"


def make_records(n: int) -> list[tuple[str, bytes]]:
    rnd = random.Random(0xC6DB)
    protocols = ["ble", "zigbee", "oui", "mdns"]
    out = []
    for i in range(n):
        proto = protocols[i % len(protocols)]
        if proto == "ble":
            key = f"ble:{rnd.randrange(0, 65536):05d}|{rnd.randrange(0,1<<32):08x}|device-{i:06d}"
        elif proto == "zigbee":
            key = f"zigbee:MFR-{rnd.randrange(0,1500):04d}\x1fMODEL-{i:06d}"
        elif proto == "oui":
            key = f"oui:{i % 0xFFFFFF:06X}"
        else:
            key = f"mdns:_service-{i % 2000}._tcp.local|model-{i:06d}"
        value = json.dumps({
            "integration": f"integration_{i % 700}",
            "vendor": f"Vendor {i % 4000}",
            "model": f"Model {i}",
            "source_id": ["ha_core", "zha_quirks", "z2m_converters", "ieee_oui"][i % 4],
            "class": "DATA_ONLY" if i % 3 else "DATA+PARSER",
        }, separators=(",", ":"), sort_keys=True).encode()
        out.append((key, value))
    out.sort(key=lambda kv: kv[0].encode())
    return out


def build_flat(path: Path, records: list[tuple[str, bytes]]) -> None:
    blob = bytearray()
    entries = bytearray()
    for key_s, value in records:
        key = key_s.encode()
        ko = len(blob)
        blob += key
        vo = len(blob)
        blob += value
        entries += ENTRY.pack(ko, len(key), 0, vo, len(value))
    bo = FH.size + len(entries)
    path.write_bytes(FH.pack(MAGIC, len(records), ENTRY.size, FH.size, bo) + entries + blob)


def flat_lookup(f, key: bytes) -> bytes | None:
    f.seek(0)
    magic, count, es, io, bo = FH.unpack(f.read(FH.size))
    if magic != MAGIC:
        raise ValueError("bad flat benchmark file")
    lo, hi = 0, count
    while lo < hi:
        mid = (lo + hi) // 2
        f.seek(io + mid * es)
        ko, kl, _, vo, vl = ENTRY.unpack(f.read(es))
        f.seek(bo + ko)
        probe = f.read(kl)
        if probe < key:
            lo = mid + 1
        else:
            hi = mid
    if lo >= count:
        return None
    f.seek(io + lo * es)
    ko, kl, _, vo, vl = ENTRY.unpack(f.read(es))
    f.seek(bo + ko)
    if f.read(kl) != key:
        return None
    f.seek(bo + vo)
    return f.read(vl)


def build_sqlite(path: Path, records: list[tuple[str, bytes]]) -> None:
    con = sqlite3.connect(path)
    con.execute("PRAGMA journal_mode=OFF")
    con.execute("PRAGMA synchronous=OFF")
    con.execute("PRAGMA page_size=4096")
    con.execute("CREATE TABLE kv(key TEXT PRIMARY KEY, value BLOB NOT NULL) WITHOUT ROWID")
    con.executemany("INSERT INTO kv VALUES(?,?)", records)
    con.commit()
    con.execute("VACUUM")
    con.close()


def shard_id(key: str) -> int:
    return binascii.crc32(key.encode()) & 0xFF


def build_msgpack(root: Path, records: list[tuple[str, bytes]]) -> tuple[int, int]:
    if msgpack is None:
        return 0, 0
    buckets: dict[int, dict[str, bytes]] = {}
    for k, v in records:
        buckets.setdefault(shard_id(k), {})[k] = v
    total = 0
    max_size = 0
    for sid, mapping in buckets.items():
        data = msgpack.packb(mapping, use_bin_type=True)
        p = root / f"{sid:02x}.mpk"
        p.write_bytes(data)
        total += len(data)
        max_size = max(max_size, len(data))
    return total, max_size


def bench(fn, keys: list[str], rounds: int = 3) -> float:
    samples = []
    for _ in range(rounds):
        start = time.perf_counter_ns()
        for k in keys:
            fn(k)
        elapsed = time.perf_counter_ns() - start
        samples.append(elapsed / len(keys) / 1000.0)
    return statistics.median(samples)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", type=int, default=50000)
    ap.add_argument("--lookups", type=int, default=3000)
    ap.add_argument("--out")
    a = ap.parse_args()
    records = make_records(a.records)
    rnd = random.Random(12345)
    keys = [records[rnd.randrange(len(records))][0] for _ in range(a.lookups)]
    with tempfile.TemporaryDirectory() as td_s:
        td = Path(td_s)
        sql = td / "db.sqlite"
        flat = td / "db.flat"
        mp = td / "mp"
        mp.mkdir()
        build_sqlite(sql, records)
        build_flat(flat, records)
        mp_total, mp_max = build_msgpack(mp, records)

        con = sqlite3.connect(f"file:{sql}?mode=ro", uri=True)
        con.execute("PRAGMA query_only=ON")
        con.execute("PRAGMA cache_size=-64")
        cur = con.cursor()
        sql_us = bench(lambda k: cur.execute("SELECT value FROM kv WHERE key=?", (k,)).fetchone(), keys)
        con.close()
        with flat.open("rb", buffering=0) as f:
            flat_us = bench(lambda k: flat_lookup(f, k.encode()), keys)
        mp_us = None
        if msgpack is not None:
            def mp_lookup(k: str):
                data = (mp / f"{shard_id(k):02x}.mpk").read_bytes()
                return msgpack.unpackb(data, raw=False).get(k)
            mp_us = bench(mp_lookup, keys)

        result = {
            "records": len(records),
            "lookups_per_round": len(keys),
            "sqlite": {
                "bytes": sql.stat().st_size,
                "median_lookup_us": round(sql_us, 2),
                "configured_cache_kib": 64,
                "page_size": 4096,
            },
            "sharded_messagepack": None if msgpack is None else {
                "bytes": mp_total,
                "median_lookup_us": round(mp_us, 2),
                "shards": 256,
                "max_shard_bytes": mp_max,
                "access": "read+decode whole selected shard",
            },
            "flat_sorted": {
                "bytes": flat.stat().st_size,
                "median_lookup_us": round(flat_us, 2),
                "index_entry_bytes": ENTRY.size,
                "access": "binary-search fixed index; seek/read candidate key/value",
            },
            "notes": [
                "Host Python timing only; use for relative comparison, not ESP32-C6 absolute latency.",
                "MessagePack represents the sharded CBOR/MessagePack design class. The benchmark script can be extended with cbor2 when available.",
                "SQLite cache is deliberately capped to 64 KiB here; library/runtime footprint is not included in file bytes.",
            ],
        }
        text = json.dumps(result, indent=2)
        print(text)
        if a.out:
            Path(a.out).write_text(text + "\n")


if __name__ == "__main__":
    main()
