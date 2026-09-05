#!/usr/bin/env python3
"""Build and report a recognition DB from already-fetched pinned upstream trees.

This script never imports or executes upstream Python. It delegates to the mechanical
AST extractors in nearby_dbgen.py and emits a machine-readable coverage proof.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
from types import SimpleNamespace

import nearby_dbgen


def iter_flat_records(path: Path):
    with path.open("rb") as f:
        h = nearby_dbgen.read_header(f)
        base = h["payload_offset"]
        f.seek(base)
        raw = f.read(nearby_dbgen.FLAT_HEADER.size)
        magic, count, entry_size, index_off, blob_off = nearby_dbgen.FLAT_HEADER.unpack(raw)
        if magic != nearby_dbgen.FLAT_MAGIC or entry_size != nearby_dbgen.FLAT_ENTRY.size:
            raise ValueError("unsupported flat payload")
        for i in range(count):
            f.seek(base + index_off + i * entry_size)
            entry_raw = f.read(entry_size)
            if len(entry_raw) != entry_size:
                raise ValueError("truncated flat index")
            key_off, key_len, _flags, val_off, val_len = nearby_dbgen.FLAT_ENTRY.unpack(entry_raw)
            f.seek(base + blob_off + key_off)
            key = f.read(key_len).decode("utf-8")
            f.seek(base + blob_off + val_off)
            value = json.loads(f.read(val_len).decode("utf-8"))
            yield key, value


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--pins", required=True)
    p.add_argument("--ha-bluetooth", required=True)
    p.add_argument("--zha-root", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--report", required=True)
    args = p.parse_args()

    pins = json.loads(Path(args.pins).read_text(encoding="utf-8"))
    ha_rev = pins["home_assistant_core"]["revision"]
    zha_rev = pins["zha_device_handlers"]["revision"]
    build_epoch = int(pins["build_epoch"])
    out = Path(args.out)

    build_args = SimpleNamespace(
        ha_bluetooth=args.ha_bluetooth,
        ha_rev=ha_rev,
        zha_root=args.zha_root,
        zha_rev=zha_rev,
        oui_csv=None,
        oui_rev="unknown",
        db_version=1,
        build_epoch=build_epoch,
        out=str(out),
    )

    t0 = time.perf_counter()
    nearby_dbgen.build(build_args)
    build_ms = round((time.perf_counter() - t0) * 1000, 3)

    t1 = time.perf_counter()
    validation = nearby_dbgen.validate_file(out, release=True)
    validation_ms = round((time.perf_counter() - t1) * 1000, 3)
    if not validation["ok"]:
        raise SystemExit("release validation failed: " + "; ".join(validation["errors"]))

    source_counts: dict[str, int] = {}
    protocol_counts: dict[str, int] = {}
    record_counts: dict[str, int] = {}
    parser_required_count = 0
    representative: dict[str, str] = {}
    record_total = 0
    for key, value in iter_flat_records(out):
        record_total += 1
        source_id = value.get("source_id", "unknown")
        source_counts[source_id] = source_counts.get(source_id, 0) + 1
        protocol = key.split(":", 1)[0]
        protocol_counts[protocol] = protocol_counts.get(protocol, 0) + 1
        record_type = value.get("record_type", "unknown")
        record_counts[record_type] = record_counts.get(record_type, 0) + 1
        if value.get("parser_id"):
            parser_required_count += 1
        representative.setdefault(record_type, key)

    lookup_smoke = []
    for record_type, key in sorted(representative.items()):
        hit = nearby_dbgen.lookup(out, key)
        lookup_smoke.append({"record_type": record_type, "key": key, "hit": hit is not None})
        if hit is None:
            raise SystemExit(f"representative lookup failed: {key}")

    manifest = validation["manifest"]
    sources = manifest.get("sources", [])
    blocked = sum(1 for src in sources if src.get("redistribution") != "allowed")
    report = {
        "report_schema": 1,
        "scope": "full pinned HA generated Bluetooth + full pinned ZHA zhaquirks tree",
        "pins": pins,
        "generator": manifest.get("generator"),
        "db": {
            "path": str(out),
            "bytes": out.stat().st_size,
            "sha256": hashlib.sha256(out.read_bytes()).hexdigest(),
            "record_count": record_total,
            "normalized_unique_key_count": record_total,
            "conflict_count": manifest.get("conflict_count", 0),
            "parser_required_count": parser_required_count,
            "blocked_nonredistributable_source_count": blocked,
        },
        "coverage": {
            "by_source": dict(sorted(source_counts.items())),
            "by_protocol": dict(sorted(protocol_counts.items())),
            "by_record_type": dict(sorted(record_counts.items())),
        },
        "timing_ms": {"build": build_ms, "release_validation": validation_ms},
        "representative_lookups": lookup_smoke,
        "release_validation": {"ok": True, "errors": []},
        "build_command": (
            "python tools/dbgen/build_pinned.py --pins tools/dbgen/pins.json "
            "--ha-bluetooth <HA>/homeassistant/generated/bluetooth.py "
            "--zha-root <ZHA>/zhaquirks --out artifacts/pinned/nearby.nbdb "
            "--report docs/db/coverage-pinned.json"
        ),
    }
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
