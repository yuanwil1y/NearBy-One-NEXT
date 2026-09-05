#!/usr/bin/env python3
"""Build and report a recognition DB from real, immutable upstream pins."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import time

import full_dbgen
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
            key_off, key_len, flags, val_off, val_len = nearby_dbgen.FLAT_ENTRY.unpack(entry_raw)
            f.seek(base + blob_off + key_off)
            key = f.read(key_len).decode("utf-8")
            f.seek(base + blob_off + val_off)
            value = json.loads(f.read(val_len).decode("utf-8"))
            yield key, value, flags


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--pins", required=True)
    p.add_argument("--ha-bluetooth", required=True)
    p.add_argument("--ha-zeroconf", required=True)
    p.add_argument("--ha-ssdp", required=True)
    p.add_argument("--ha-dhcp", required=True)
    p.add_argument("--zha-root", required=True)
    p.add_argument("--z2m-root", required=True)
    p.add_argument("--bt-company-ids", required=True)
    p.add_argument("--bt-service-uuids", required=True)
    p.add_argument("--zigbee-manufacturer-codes", required=True)
    p.add_argument("--zigbee-consts", required=True)
    p.add_argument("--matter-root", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--report", required=True)
    args = p.parse_args()

    pins = json.loads(Path(args.pins).read_text(encoding="utf-8"))
    ha_rev = pins["home_assistant_core"]["revision"]
    zha_rev = pins["zha_device_handlers"]["revision"]
    z2m_rev = pins["zigbee_herdsman_converters"]["revision"]
    bt_rev = pins["bluetooth_numbers_nordic"]["revision"]
    zigbee_defs_rev = pins["zigbee_herdsman_defs"]["revision"]
    matter_rev = pins["smartthings_matter_fingerprints"]["revision"]
    build_epoch = int(pins["build_epoch"])
    out = Path(args.out)

    t0 = time.perf_counter()
    build_summary = full_dbgen.build_database(
        ha_bluetooth=Path(args.ha_bluetooth),
        ha_zeroconf=Path(args.ha_zeroconf),
        ha_ssdp=Path(args.ha_ssdp),
        ha_dhcp=Path(args.ha_dhcp),
        ha_rev=ha_rev,
        zha_root=Path(args.zha_root),
        zha_rev=zha_rev,
        z2m_root=Path(args.z2m_root),
        z2m_rev=z2m_rev,
        bt_company_ids=Path(args.bt_company_ids),
        bt_service_uuids=Path(args.bt_service_uuids),
        bt_rev=bt_rev,
        zigbee_manufacturer_codes=Path(args.zigbee_manufacturer_codes),
        zigbee_consts=Path(args.zigbee_consts),
        zigbee_defs_rev=zigbee_defs_rev,
        matter_root=Path(args.matter_root),
        matter_rev=matter_rev,
        out=out,
        db_version=2,
        build_epoch=build_epoch,
    )
    build_ms = round((time.perf_counter() - t0) * 1000, 3)

    t1 = time.perf_counter()
    validation = nearby_dbgen.validate_file(out, release=True)
    validation_ms = round((time.perf_counter() - t1) * 1000, 3)
    if not validation["ok"]:
        raise SystemExit("release validation failed: " + "; ".join(validation["errors"]))

    protocol_counts: dict[str, int] = {}
    record_counts: dict[str, int] = {}
    parser_required_count = 0
    representative: dict[str, str] = {}
    ambiguity_flag_count = 0
    record_total = 0
    for key, value, flags in iter_flat_records(out):
        record_total += 1
        protocol = key.split(":", 1)[0]
        protocol_counts[protocol] = protocol_counts.get(protocol, 0) + 1
        record_type = value.get("record_type", "unknown")
        record_counts[record_type] = record_counts.get(record_type, 0) + 1
        is_ambiguous = value.get("match_status") == "ambiguous"
        flag_ambiguous = bool(flags & full_dbgen.AMBIGUOUS_INDEX_FLAG)
        if is_ambiguous != flag_ambiguous:
            raise SystemExit(f"ambiguity flag/value mismatch: {key}")
        if flag_ambiguous:
            ambiguity_flag_count += 1
            for candidate in value.get("candidates", []):
                if isinstance(candidate, dict) and candidate.get("parser_id"):
                    parser_required_count += 1
        elif value.get("parser_id"):
            parser_required_count += 1
        representative.setdefault(record_type, key)

    lookup_smoke = []
    for record_type, key in sorted(representative.items()):
        value = nearby_dbgen.lookup(out, key)
        if value is None:
            status = "not_found"
        elif value.get("match_status") == "ambiguous":
            status = "ambiguous"
        else:
            status = "matched"
        lookup_smoke.append({"record_type": record_type, "key": key, "status": status})
        if status == "not_found":
            raise SystemExit(f"representative lookup failed: {key}")
        if record_type == "recognition_ambiguity" and status != "ambiguous":
            raise SystemExit(f"ambiguous representative returned as confirmed match: {key}")

    manifest = validation["manifest"]
    sources = manifest.get("sources", [])
    blocked = sum(1 for src in sources if src.get("redistribution") != "allowed")
    report = {
        "report_schema": 6,
        "scope": (
            "full pinned HA generated Bluetooth/Zeroconf/SSDP/DHCP + full pinned ZHA + "
            "static pinned Z2M identity extraction + pinned redistributable Bluetooth assigned numbers + "
            "pinned MIT Zigbee manufacturer/profile identifiers + pinned Apache-2.0 SmartThings Matter VID/PID fingerprints"
        ),
        "pins": pins,
        "generator": manifest.get("generator"),
        "db": {
            "path": str(out),
            "bytes": out.stat().st_size,
            "sha256": hashlib.sha256(out.read_bytes()).hexdigest(),
            "record_count": record_total,
            "raw_record_count": build_summary["raw_records"],
            "normalized_unique_key_count": record_total,
            "conflict_count": manifest.get("conflict_count", 0),
            "ambiguous_key_count": manifest.get("ambiguous_key_count", 0),
            "ambiguity_index_flag_count": ambiguity_flag_count,
            "conflict_ledger": manifest.get("conflict_ledger", []),
            "parser_required_claim_count": parser_required_count,
            "blocked_nonredistributable_source_count": blocked,
        },
        "coverage": {
            "by_source_raw_claims": build_summary["raw_source_counts"],
            "by_protocol_runtime_keys": dict(sorted(protocol_counts.items())),
            "by_record_type_runtime_keys": dict(sorted(record_counts.items())),
        },
        "extraction": manifest.get("extraction", {}),
        "timing_ms": {"build": build_ms, "release_validation": validation_ms},
        "representative_lookups": lookup_smoke,
        "release_validation": {"ok": True, "errors": []},
        "build_command": (
            "python tools/dbgen/build_pinned.py --pins tools/dbgen/pins.json "
            "--ha-bluetooth <HA>/homeassistant/generated/bluetooth.py "
            "--ha-zeroconf <HA>/homeassistant/generated/zeroconf.py "
            "--ha-ssdp <HA>/homeassistant/generated/ssdp.py "
            "--ha-dhcp <HA>/homeassistant/generated/dhcp.py "
            "--zha-root <ZHA>/zhaquirks --z2m-root <Z2M>/src/devices "
            "--bt-company-ids <BT>/v1/company_ids.json --bt-service-uuids <BT>/v1/service_uuids.json "
            "--zigbee-manufacturer-codes <ZH>/src/zspec/zcl/definition/manufacturerCode.ts "
            "--zigbee-consts <ZH>/src/zspec/consts.ts --matter-root <ST>/drivers "
            "--out artifacts/pinned/nearby.nbdb --report docs/db/coverage-pinned.json"
        ),
    }
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
