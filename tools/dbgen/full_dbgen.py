"""Build the combined NearBy recognition container from mechanical extractors.

This module is PC-side tooling only. It consumes already-fetched/pinned source files
and never imports or executes Home Assistant, ZHA, or Zigbee2MQTT runtime code.
"""
from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable

import ha_lan_sources
import nearby_dbgen
import z2m_sources


COLLISION_WINNER_REASON = (
    "deterministic canonical-JSON lexical tiebreak; no semantic merge; manual review required"
)


def _source_manifest(ha_rev: str, zha_rev: str, z2m_rev: str) -> list[dict[str, Any]]:
    return [
        {
            "source_id": "ha_core",
            "name": "Home Assistant Core",
            "upstream_revision": ha_rev,
            "license_spdx": "Apache-2.0",
            "redistribution": "allowed",
            "classification": "DATA_ONLY",
            "transform": "extract_ha_generated_discovery_ast",
        },
        {
            "source_id": "zha_quirks",
            "name": "zigpy/zha-device-handlers",
            "upstream_revision": zha_rev,
            "license_spdx": "Apache-2.0",
            "redistribution": "allowed",
            "classification": "DATA+PARSER",
            "transform": "extract_zha_literal_identifiers_ast",
        },
        {
            "source_id": "z2m_converters",
            "name": "Koenkk/zigbee-herdsman-converters",
            "upstream_revision": z2m_rev,
            "license_spdx": "MIT",
            "redistribution": "allowed",
            "classification": "DATA+PARSER",
            "transform": "extract_z2m_static_typescript_identity",
        },
    ]


def dedupe_with_ledger(
    records: Iterable[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    grouped: dict[str, dict[bytes, dict[str, Any]]] = {}
    for rec in records:
        key = rec["key"]
        grouped.setdefault(key, {})[nearby_dbgen.canonical_json(rec)] = rec

    winners: list[dict[str, Any]] = []
    ledger: list[dict[str, Any]] = []
    for key in sorted(grouped):
        candidates = [
            rec for _canonical, rec in sorted(grouped[key].items(), key=lambda item: item[0])
        ]
        winner = candidates[0]
        winners.append(winner)
        if len(candidates) > 1:
            ledger.append(
                {
                    "key": key,
                    "sources": sorted({str(rec.get("source_id", "unknown")) for rec in candidates}),
                    "candidate_count": len(candidates),
                    "candidates": candidates,
                    "winner": winner,
                    "winner_reason": COLLISION_WINNER_REASON,
                    "manual_review": True,
                }
            )
    return winners, ledger


def build_database(
    *,
    ha_bluetooth: Path,
    ha_zeroconf: Path,
    ha_ssdp: Path,
    ha_dhcp: Path,
    ha_rev: str,
    zha_root: Path,
    zha_rev: str,
    z2m_root: Path,
    z2m_rev: str,
    out: Path,
    db_version: int,
    build_epoch: int,
) -> dict[str, Any]:
    by_extractor: dict[str, list[dict[str, Any]]] = {
        "ha_bluetooth": nearby_dbgen.extract_ha_bluetooth(ha_bluetooth, ha_rev),
        "ha_homekit": ha_lan_sources.extract_ha_homekit(ha_zeroconf, ha_rev),
        "ha_zeroconf": ha_lan_sources.extract_ha_zeroconf(ha_zeroconf, ha_rev),
        "ha_ssdp": ha_lan_sources.extract_ha_ssdp(ha_ssdp, ha_rev),
        "ha_dhcp": ha_lan_sources.extract_ha_dhcp(ha_dhcp, ha_rev),
        "zha": nearby_dbgen.extract_zha(zha_root, zha_rev),
    }
    z2m_records, z2m_stats = z2m_sources.extract_z2m(z2m_root, z2m_rev)
    by_extractor["z2m"] = z2m_records

    raw_records: list[dict[str, Any]] = []
    raw_counts: dict[str, int] = {}
    for name, records in by_extractor.items():
        raw_counts[name] = len(records)
        raw_records.extend(records)

    records, conflict_ledger = dedupe_with_ledger(raw_records)
    payload = nearby_dbgen.build_flat_payload(records)
    sources = _source_manifest(ha_rev, zha_rev, z2m_rev)

    counts: dict[str, int] = {}
    for rec in records:
        record_type = rec["record_type"]
        counts[record_type] = counts.get(record_type, 0) + 1

    manifest = {
        "container": "nearby-recognition-db",
        "format": "flat-v0",
        "schema_version": nearby_dbgen.SCHEMA_VERSION,
        "db_version": db_version,
        "reader_abi": nearby_dbgen.READER_ABI,
        "record_count": len(records),
        "raw_record_count": len(raw_records),
        "record_counts": dict(sorted(counts.items())),
        "conflict_count": len(conflict_ledger),
        "conflict_keys_sample": [entry["key"] for entry in conflict_ledger[:50]],
        "conflict_ledger": conflict_ledger,
        "sources": sources,
        "extraction": {
            "raw_records_by_extractor": dict(sorted(raw_counts.items())),
            "z2m": z2m_stats,
        },
        "payload": {"encoding": "sorted-flat-records-v1", "key_order": "utf8-byte-lexicographic"},
        "generator": {"name": "full_dbgen.py", "version": 3},
    }
    manifest_bytes = nearby_dbgen.canonical_json(manifest)
    file_size = nearby_dbgen.HEADER_SIZE + len(manifest_bytes) + len(payload)
    header = nearby_dbgen.make_header(
        db_version=db_version,
        source_count=len(sources),
        file_size=file_size,
        manifest_size=len(manifest_bytes),
        payload=payload,
        build_epoch=build_epoch,
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + manifest_bytes + payload)
    return {
        "bytes": file_size,
        "records": len(records),
        "raw_records": len(raw_records),
        "counts": dict(sorted(counts.items())),
        "conflicts": len(conflict_ledger),
        "conflict_ledger": conflict_ledger,
        "extraction": manifest["extraction"],
    }
