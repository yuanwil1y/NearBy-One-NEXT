"""Build the combined NearBy recognition container from mechanical extractors.

This module is PC-side tooling only. It consumes already-fetched/pinned source files
and never imports or executes Home Assistant, ZHA, Zigbee2MQTT, herdsman, or
SmartThings runtime code.
"""
from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable

import bt_numbers_sources
import ha_lan_sources
import matter_sources
import nearby_dbgen
import z2m_sources
import zigbee_numbers_sources

AMBIGUOUS_INDEX_FLAG = 0x0001
CANDIDATE_ORDER_REASON = "canonical-JSON lexical order for reproducibility only; not recognition precedence"


def _source_manifest(
    ha_rev: str,
    zha_rev: str,
    z2m_rev: str,
    bt_rev: str | None,
    zigbee_defs_rev: str | None,
    matter_rev: str | None,
) -> list[dict[str, Any]]:
    sources = [
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
    if bt_rev is not None:
        sources.append(
            {
                "source_id": "bluetooth_numbers_nordic",
                "name": "Nordic Semiconductor bluetooth-numbers-database",
                "upstream_revision": bt_rev,
                "upstream_url": "https://github.com/nordicsemi/bluetooth-numbers-database",
                "license_spdx": "BSD-3-Clause",
                "license_url": "https://github.com/nordicsemi/bluetooth-numbers-database/blob/master/LICENSE",
                "redistribution": "allowed",
                "classification": "DATA_ONLY",
                "transform": "extract_bt_numbers_json_company_and_service",
            }
        )
    if zigbee_defs_rev is not None:
        sources.append(
            {
                "source_id": "zigbee_herdsman_defs",
                "name": "Koenkk/zigbee-herdsman static Zigbee definitions",
                "upstream_revision": zigbee_defs_rev,
                "upstream_url": "https://github.com/Koenkk/zigbee-herdsman",
                "license_spdx": "MIT",
                "license_url": "https://github.com/Koenkk/zigbee-herdsman/blob/master/LICENSE",
                "redistribution": "allowed",
                "classification": "DATA_ONLY",
                "transform": "extract_zigbee_manufacturer_codes_and_public_profile_ids",
            }
        )
    if matter_rev is not None:
        sources.append(
            {
                "source_id": "smartthings_matter_fingerprints",
                "name": "SmartThings Edge Drivers Matter manufacturer fingerprints",
                "upstream_revision": matter_rev,
                "upstream_url": "https://github.com/SmartThingsCommunity/SmartThingsEdgeDrivers",
                "license_spdx": "Apache-2.0",
                "license_url": "https://github.com/SmartThingsCommunity/SmartThingsEdgeDrivers/blob/main/LICENSE",
                "redistribution": "allowed",
                "classification": "DATA_ONLY",
                "transform": "extract_matter_manufacturer_vid_pid_fingerprints",
                "catalog_semantics": "integration_fingerprint_not_authoritative_assignment_registry",
            }
        )
    return sources


def _ambiguity_record(key: str, candidates: list[dict[str, Any]]) -> dict[str, Any]:
    source_ids = sorted({str(rec.get("source_id", "unknown")) for rec in candidates})
    return {
        "key": key,
        "record_type": "recognition_ambiguity",
        "match_status": "ambiguous",
        "candidate_count": len(candidates),
        "source_ids": source_ids,
        "candidate_order": CANDIDATE_ORDER_REASON,
        "candidates": candidates,
    }


def dedupe_with_ledger(
    records: Iterable[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Deduplicate exact claims and fail closed for unresolved key collisions."""
    grouped: dict[str, dict[bytes, dict[str, Any]]] = {}
    for rec in records:
        key = rec["key"]
        grouped.setdefault(key, {})[nearby_dbgen.canonical_json(rec)] = rec

    runtime_records: list[dict[str, Any]] = []
    ledger: list[dict[str, Any]] = []
    for key in sorted(grouped):
        candidates = [
            rec for _canonical, rec in sorted(grouped[key].items(), key=lambda item: item[0])
        ]
        if len(candidates) == 1:
            runtime_records.append(candidates[0])
            continue

        ambiguity = _ambiguity_record(key, candidates)
        runtime_records.append(ambiguity)
        ledger.append(
            {
                "key": key,
                "sources": ambiguity["source_ids"],
                "candidate_count": len(candidates),
                "candidates": candidates,
                "resolution": "unresolved_ambiguous",
                "runtime_result": "ambiguous",
                "candidate_order": CANDIDATE_ORDER_REASON,
                "manual_review": True,
            }
        )
    return runtime_records, ledger


def build_flat_payload(records: list[dict[str, Any]]) -> bytes:
    """Build the flat payload and mark ambiguity in the index itself."""
    blob = bytearray()
    entries = bytearray()
    for rec in records:
        key = rec["key"].encode("utf-8")
        value = nearby_dbgen.canonical_json({k: v for k, v in rec.items() if k != "key"})
        if len(key) > 0xFFFF:
            raise ValueError("key too long")
        key_off = len(blob)
        blob += key
        value_off = len(blob)
        blob += value
        flags = AMBIGUOUS_INDEX_FLAG if rec.get("match_status") == "ambiguous" else 0
        entries += nearby_dbgen.FLAT_ENTRY.pack(key_off, len(key), flags, value_off, len(value))
    blob_offset = nearby_dbgen.FLAT_HEADER.size + len(entries)
    return nearby_dbgen.FLAT_HEADER.pack(
        nearby_dbgen.FLAT_MAGIC,
        len(records),
        nearby_dbgen.FLAT_ENTRY.size,
        nearby_dbgen.FLAT_HEADER.size,
        blob_offset,
    ) + entries + blob


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
    bt_company_ids: Path | None = None,
    bt_service_uuids: Path | None = None,
    bt_rev: str | None = None,
    zigbee_manufacturer_codes: Path | None = None,
    zigbee_consts: Path | None = None,
    zigbee_defs_rev: str | None = None,
    matter_root: Path | None = None,
    matter_rev: str | None = None,
) -> dict[str, Any]:
    if (bt_company_ids is None) != (bt_service_uuids is None):
        raise ValueError("Bluetooth assigned-number company/service inputs must be supplied together")
    if bt_company_ids is not None and bt_rev is None:
        raise ValueError("Bluetooth assigned-number revision is required when inputs are supplied")
    if (zigbee_manufacturer_codes is None) != (zigbee_consts is None):
        raise ValueError("Zigbee manufacturer/profile definition inputs must be supplied together")
    if zigbee_manufacturer_codes is not None and zigbee_defs_rev is None:
        raise ValueError("Zigbee definition revision is required when inputs are supplied")
    if matter_root is not None and matter_rev is None:
        raise ValueError("Matter fingerprint revision is required when inputs are supplied")

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
    if bt_company_ids is not None and bt_service_uuids is not None and bt_rev is not None:
        by_extractor["bluetooth_assigned_numbers"] = (
            bt_numbers_sources.extract_company_ids(bt_company_ids, bt_rev)
            + bt_numbers_sources.extract_service_uuids(bt_service_uuids, bt_rev)
        )
    if zigbee_manufacturer_codes is not None and zigbee_consts is not None and zigbee_defs_rev is not None:
        by_extractor["zigbee_assigned_numbers"] = (
            zigbee_numbers_sources.extract_manufacturer_codes(zigbee_manufacturer_codes, zigbee_defs_rev)
            + zigbee_numbers_sources.extract_profile_ids(zigbee_consts, zigbee_defs_rev)
        )
    matter_stats: dict[str, int] = {}
    if matter_root is not None and matter_rev is not None:
        matter_records, matter_stats = matter_sources.extract_tree(matter_root, matter_rev)
        by_extractor["smartthings_matter_fingerprints"] = matter_records

    raw_records: list[dict[str, Any]] = []
    raw_counts: dict[str, int] = {}
    raw_source_counts: dict[str, int] = {}
    for name, records in by_extractor.items():
        raw_counts[name] = len(records)
        raw_records.extend(records)
        for rec in records:
            source_id = str(rec.get("source_id", "unknown"))
            raw_source_counts[source_id] = raw_source_counts.get(source_id, 0) + 1

    records, conflict_ledger = dedupe_with_ledger(raw_records)
    payload = build_flat_payload(records)
    sources = _source_manifest(
        ha_rev,
        zha_rev,
        z2m_rev,
        bt_rev if bt_company_ids is not None else None,
        zigbee_defs_rev if zigbee_manufacturer_codes is not None else None,
        matter_rev if matter_root is not None else None,
    )

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
        "raw_source_counts": dict(sorted(raw_source_counts.items())),
        "conflict_count": len(conflict_ledger),
        "ambiguous_key_count": len(conflict_ledger),
        "conflict_keys_sample": [entry["key"] for entry in conflict_ledger[:50]],
        "conflict_ledger": conflict_ledger,
        "conflict_policy": {
            "unresolved_collision": "return_ambiguity",
            "lexical_order_is_precedence": False,
            "index_ambiguous_flag": AMBIGUOUS_INDEX_FLAG,
        },
        "sources": sources,
        "extraction": {
            "raw_records_by_extractor": dict(sorted(raw_counts.items())),
            "z2m": z2m_stats,
            "matter": matter_stats,
        },
        "payload": {
            "encoding": "sorted-flat-records-v1",
            "key_order": "utf8-byte-lexicographic",
            "index_flags": {"0x0001": "ambiguous; value ref valid; not a confirmed match"},
        },
        "generator": {"name": "full_dbgen.py", "version": 7},
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
        "raw_source_counts": dict(sorted(raw_source_counts.items())),
        "counts": dict(sorted(counts.items())),
        "conflicts": len(conflict_ledger),
        "ambiguous_keys": len(conflict_ledger),
        "conflict_ledger": conflict_ledger,
        "extraction": manifest["extraction"],
    }
