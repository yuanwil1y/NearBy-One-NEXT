#!/usr/bin/env python3
"""NearBy Recognition DB v0 generator/validator PoC.

Stdlib-only on purpose: build machines may point this tool at upstream checkouts.
It never imports Home Assistant or ZHA; it mechanically extracts literal/generated
metadata and writes a read-only, seekable flat record container.
"""
from __future__ import annotations

import argparse
import ast
import binascii
import csv
import hashlib
import json
from pathlib import Path
import struct
import time
from typing import Any, Iterable

MAGIC = b"NBYDB\0\r\n"
FLAT_MAGIC = b"NBYFLT1\0"
HEADER_SIZE = 128
FORMAT_MAJOR = 1
FORMAT_MINOR = 0
SCHEMA_VERSION = 1
READER_ABI = 1
FLAG_SHA256 = 1
HEADER = struct.Struct("<8sHHHHIIIIQQQQQII32sQ8s")
FLAT_HEADER = struct.Struct("<8sIIQQ")
FLAT_ENTRY = struct.Struct("<IHHII")
assert HEADER.size == HEADER_SIZE
assert FLAT_HEADER.size == 32
assert FLAT_ENTRY.size == 16


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def simple_constants(tree: ast.AST) -> dict[str, str | int | bool]:
    out: dict[str, str | int | bool] = {}
    for node in getattr(tree, "body", []):
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if len(targets) != 1 or not isinstance(targets[0], ast.Name) or node.value is None:
                continue
            if isinstance(node.value, ast.Constant) and isinstance(node.value.value, (str, int, bool)):
                out[targets[0].id] = node.value.value
    return out


def resolve_literal(node: ast.AST, constants: dict[str, Any]) -> Any | None:
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return constants.get(node.id)
    return None


def extract_ha_bluetooth(path: Path, revision: str) -> list[dict[str, Any]]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    value: ast.AST | None = None
    for node in tree.body:
        if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name) and node.target.id == "BLUETOOTH":
            value = node.value
            break
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "BLUETOOTH" for t in node.targets):
            value = node.value
            break
    if value is None:
        raise ValueError(f"BLUETOOTH assignment not found in {path}")
    raw = ast.literal_eval(value)
    records: list[dict[str, Any]] = []
    for i, matcher in enumerate(raw):
        if not isinstance(matcher, dict) or "domain" not in matcher:
            continue
        normalized = dict(matcher)
        normalized.update(
            source_id="ha_core",
            source_revision=revision,
            classification="DATA_ONLY",
            record_type="ha_bluetooth_matcher",
        )
        key_parts = [
            str(normalized.get("manufacturer_id", "")),
            str(normalized.get("service_uuid", "")),
            str(normalized.get("service_data_uuid", "")),
            str(normalized.get("local_name", "")),
            str(normalized.get("domain", "")),
            str(i),
        ]
        normalized["key"] = "ble:" + "|".join(key_parts)
        records.append(normalized)
    return records


def _models_info_pairs(tree: ast.AST, constants: dict[str, Any]) -> Iterable[tuple[str, str]]:
    for node in ast.walk(tree):
        if not isinstance(node, ast.Dict):
            continue
        for key_node, value_node in zip(node.keys, node.values):
            is_models = isinstance(key_node, ast.Name) and key_node.id == "MODELS_INFO"
            is_models = is_models or (isinstance(key_node, ast.Constant) and key_node.value in ("models_info", "MODELS_INFO"))
            if not is_models or not isinstance(value_node, (ast.List, ast.Tuple)):
                continue
            for elt in value_node.elts:
                if not isinstance(elt, (ast.Tuple, ast.List)) or len(elt.elts) < 2:
                    continue
                manufacturer = resolve_literal(elt.elts[0], constants)
                model = resolve_literal(elt.elts[1], constants)
                if isinstance(manufacturer, str) and isinstance(model, str):
                    yield manufacturer, model


def _quirkbuilder_pairs(tree: ast.AST, constants: dict[str, Any]) -> Iterable[tuple[str, str]]:
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or len(node.args) < 2:
            continue
        func_name = None
        if isinstance(node.func, ast.Name):
            func_name = node.func.id
        elif isinstance(node.func, ast.Attribute):
            func_name = node.func.attr
        if func_name not in {"QuirkBuilder", "applies_to", "also_applies_to"}:
            continue
        manufacturer = resolve_literal(node.args[0], constants)
        model = resolve_literal(node.args[1], constants)
        if isinstance(manufacturer, str) and isinstance(model, str):
            yield manufacturer, model


def extract_zha(root: Path, revision: str) -> list[dict[str, Any]]:
    pairs: set[tuple[str, str, str]] = set()
    for path in sorted(root.rglob("*.py")):
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (SyntaxError, UnicodeDecodeError):
            continue
        constants = simple_constants(tree)
        rel = path.relative_to(root).as_posix()
        for manufacturer, model in _models_info_pairs(tree, constants):
            pairs.add((manufacturer, model, rel))
        for manufacturer, model in _quirkbuilder_pairs(tree, constants):
            pairs.add((manufacturer, model, rel))
    records = []
    for manufacturer, model, rel in sorted(pairs):
        records.append({
            "key": f"zigbee:{manufacturer}\x1f{model}",
            "record_type": "zha_manufacturer_model",
            "manufacturer": manufacturer,
            "model": model,
            "source_path": rel,
            "source_id": "zha_quirks",
            "source_revision": revision,
            "classification": "DATA+PARSER",
            "parser_id": "zha_quirk_optional",
        })
    return records


def _pick(row: dict[str, str], names: tuple[str, ...]) -> str:
    lowered = {k.strip().lower(): (v or "").strip() for k, v in row.items()}
    for name in names:
        if name in lowered and lowered[name]:
            return lowered[name]
    return ""


def extract_oui(path: Path, revision: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            assignment = _pick(row, ("assignment", "oui", "prefix", "mac prefix"))
            org = _pick(row, ("organization name", "organization", "company", "vendor"))
            registry = _pick(row, ("registry", "block type"))
            prefix = "".join(ch for ch in assignment.upper() if ch in "0123456789ABCDEF")
            if len(prefix) not in (6, 7, 9) or not org:
                continue
            records.append({
                "key": f"oui:{prefix}",
                "record_type": "oui_assignment",
                "prefix": prefix,
                "prefix_bits": len(prefix) * 4,
                "organization": org,
                "registry": registry,
                "source_id": "ieee_oui",
                "source_revision": revision,
                "classification": "DATA_ONLY",
            })
    return records


def dedupe(records: Iterable[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[str]]:
    by_key: dict[str, dict[str, Any]] = {}
    conflicts: list[str] = []
    for rec in records:
        key = rec["key"]
        if key in by_key and canonical_json(by_key[key]) != canonical_json(rec):
            conflicts.append(key)
            if canonical_json(rec) < canonical_json(by_key[key]):
                by_key[key] = rec
        else:
            by_key[key] = rec
    return [by_key[k] for k in sorted(by_key)], sorted(set(conflicts))


def build_flat_payload(records: list[dict[str, Any]]) -> bytes:
    blob = bytearray()
    entries = bytearray()
    for rec in records:
        key = rec["key"].encode("utf-8")
        value = canonical_json({k: v for k, v in rec.items() if k != "key"})
        if len(key) > 0xFFFF:
            raise ValueError("key too long")
        key_off = len(blob)
        blob += key
        value_off = len(blob)
        blob += value
        entries += FLAT_ENTRY.pack(key_off, len(key), 0, value_off, len(value))
    blob_offset = FLAT_HEADER.size + len(entries)
    return FLAT_HEADER.pack(FLAT_MAGIC, len(records), FLAT_ENTRY.size, FLAT_HEADER.size, blob_offset) + entries + blob


def source_table(args: argparse.Namespace) -> list[dict[str, Any]]:
    out = []
    if args.ha_bluetooth:
        out.append({
            "source_id": "ha_core", "name": "Home Assistant Core", "upstream_revision": args.ha_rev,
            "license_spdx": "Apache-2.0", "redistribution": "allowed", "classification": "DATA_ONLY",
            "transform": "extract_ha_bluetooth_ast",
        })
    if args.zha_root:
        out.append({
            "source_id": "zha_quirks", "name": "zigpy/zha-device-handlers", "upstream_revision": args.zha_rev,
            "license_spdx": "Apache-2.0", "redistribution": "allowed", "classification": "DATA+PARSER",
            "transform": "extract_zha_literal_identifiers_ast",
        })
    if args.oui_csv:
        out.append({
            "source_id": "ieee_oui", "name": "IEEE OUI Registry", "upstream_revision": args.oui_rev,
            "license_spdx": "LicenseRef-IEEE-OUI-Terms", "redistribution": "review-required", "classification": "DATA_ONLY",
            "transform": "extract_ieee_style_oui_csv",
        })
    return out


def make_header(*, db_version: int, source_count: int, file_size: int, manifest_size: int,
                payload: bytes, build_epoch: int) -> bytes:
    payload_offset = HEADER_SIZE + manifest_size
    payload_crc = binascii.crc32(payload) & 0xFFFFFFFF
    payload_sha = hashlib.sha256(payload).digest()
    raw = HEADER.pack(
        MAGIC, FORMAT_MAJOR, FORMAT_MINOR, HEADER_SIZE, FLAG_SHA256,
        SCHEMA_VERSION, db_version, READER_ABI, source_count,
        file_size, HEADER_SIZE, manifest_size, payload_offset, len(payload),
        payload_crc, 0, payload_sha, build_epoch, b"\0" * 8,
    )
    crc = binascii.crc32(raw) & 0xFFFFFFFF
    return raw[:76] + struct.pack("<I", crc) + raw[80:]


def build(args: argparse.Namespace) -> None:
    records: list[dict[str, Any]] = []
    if args.ha_bluetooth:
        records += extract_ha_bluetooth(Path(args.ha_bluetooth), args.ha_rev)
    if args.zha_root:
        records += extract_zha(Path(args.zha_root), args.zha_rev)
    if args.oui_csv:
        records += extract_oui(Path(args.oui_csv), args.oui_rev)
    records, conflicts = dedupe(records)
    payload = build_flat_payload(records)
    sources = source_table(args)
    counts: dict[str, int] = {}
    for rec in records:
        counts[rec["record_type"]] = counts.get(rec["record_type"], 0) + 1
    manifest = {
        "container": "nearby-recognition-db",
        "format": "flat-v0",
        "schema_version": SCHEMA_VERSION,
        "db_version": args.db_version,
        "reader_abi": READER_ABI,
        "record_count": len(records),
        "record_counts": counts,
        "conflict_count": len(conflicts),
        "conflict_keys_sample": conflicts[:50],
        "sources": sources,
        "payload": {"encoding": "sorted-flat-records-v1", "key_order": "utf8-byte-lexicographic"},
        "generator": {"name": "nearby_dbgen.py", "version": 1},
    }
    manifest_bytes = canonical_json(manifest)
    file_size = HEADER_SIZE + len(manifest_bytes) + len(payload)
    header = make_header(
        db_version=args.db_version, source_count=len(sources), file_size=file_size,
        manifest_size=len(manifest_bytes), payload=payload, build_epoch=args.build_epoch or int(time.time()),
    )
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + manifest_bytes + payload)
    print(json.dumps({"out": str(out), "bytes": file_size, "records": len(records), "counts": counts, "conflicts": len(conflicts)}, indent=2))


def read_header(f) -> dict[str, Any]:
    raw = f.read(HEADER_SIZE)
    if len(raw) != HEADER_SIZE:
        raise ValueError("file shorter than 128-byte header")
    fields = HEADER.unpack(raw)
    names = [
        "magic", "format_major", "format_minor", "header_size", "flags", "schema_version", "db_version",
        "min_reader_abi", "source_count", "file_size", "manifest_offset", "manifest_size", "payload_offset",
        "payload_size", "payload_crc32", "header_crc32", "payload_sha256", "build_epoch", "reserved",
    ]
    h = dict(zip(names, fields))
    zeroed = raw[:76] + b"\0\0\0\0" + raw[80:]
    h["computed_header_crc32"] = binascii.crc32(zeroed) & 0xFFFFFFFF
    return h


def validate_file(path: Path, release: bool = False) -> dict[str, Any]:
    size = path.stat().st_size
    with path.open("rb") as f:
        h = read_header(f)
        errors = []
        if h["magic"] != MAGIC:
            errors.append("bad magic")
        if h["format_major"] != FORMAT_MAJOR:
            errors.append("unsupported format_major")
        if h["header_size"] != HEADER_SIZE:
            errors.append("bad header_size")
        if h["schema_version"] != SCHEMA_VERSION:
            errors.append("unsupported schema_version")
        if h["min_reader_abi"] > READER_ABI:
            errors.append("reader ABI too old")
        if h["file_size"] != size:
            errors.append("file_size mismatch")
        if h["header_crc32"] != h["computed_header_crc32"]:
            errors.append("header CRC32 mismatch")
        mo, ms, po, ps = h["manifest_offset"], h["manifest_size"], h["payload_offset"], h["payload_size"]
        if mo != HEADER_SIZE or mo + ms != po or po + ps != size:
            errors.append("invalid manifest/payload ranges")
        if h["reserved"] != b"\0" * 8:
            errors.append("reserved header bytes non-zero")
        manifest: dict[str, Any] = {}
        if not errors:
            f.seek(mo)
            try:
                manifest = json.loads(f.read(ms).decode("utf-8"))
            except Exception as exc:
                errors.append(f"manifest parse failed: {exc}")
        if manifest:
            sources = manifest.get("sources", [])
            if len(sources) != h["source_count"]:
                errors.append("source_count mismatch")
            required = {"source_id", "upstream_revision", "license_spdx", "redistribution", "classification", "transform"}
            for src in sources:
                missing = required - src.keys()
                if missing:
                    errors.append(f"source {src.get('source_id','?')} missing {sorted(missing)}")
                if release and src.get("redistribution") != "allowed":
                    errors.append(f"source {src.get('source_id','?')} is not release-allowed")
        if not errors:
            f.seek(po)
            crc = 0
            sha = hashlib.sha256()
            remaining = ps
            while remaining:
                chunk = f.read(min(16384, remaining))
                if not chunk:
                    errors.append("unexpected EOF in payload")
                    break
                remaining -= len(chunk)
                crc = binascii.crc32(chunk, crc)
                sha.update(chunk)
            crc &= 0xFFFFFFFF
            if crc != h["payload_crc32"]:
                errors.append("payload CRC32 mismatch")
            if h["flags"] & FLAG_SHA256 and sha.digest() != h["payload_sha256"]:
                errors.append("payload SHA256 mismatch")
        return {
            "ok": not errors,
            "errors": errors,
            "header": {k: (v.hex() if isinstance(v, bytes) else v) for k, v in h.items() if k not in {"reserved"}},
            "manifest": manifest,
        }


def lookup(path: Path, key_text: str) -> dict[str, Any] | None:
    key = key_text.encode("utf-8")
    with path.open("rb") as f:
        h = read_header(f)
        f.seek(h["payload_offset"])
        flat_raw = f.read(FLAT_HEADER.size)
        magic, count, entry_size, index_off, blob_off = FLAT_HEADER.unpack(flat_raw)
        if magic != FLAT_MAGIC or entry_size != FLAT_ENTRY.size:
            raise ValueError("unsupported flat payload")
        payload_base = h["payload_offset"]
        lo, hi = 0, count
        while lo < hi:
            mid = (lo + hi) // 2
            f.seek(payload_base + index_off + mid * entry_size)
            key_off, key_len, _flags, val_off, val_len = FLAT_ENTRY.unpack(f.read(entry_size))
            f.seek(payload_base + blob_off + key_off)
            probe = f.read(key_len)
            if probe < key:
                lo = mid + 1
            else:
                hi = mid
        if lo >= count:
            return None
        f.seek(payload_base + index_off + lo * entry_size)
        key_off, key_len, _flags, val_off, val_len = FLAT_ENTRY.unpack(f.read(entry_size))
        f.seek(payload_base + blob_off + key_off)
        if f.read(key_len) != key:
            return None
        f.seek(payload_base + blob_off + val_off)
        return json.loads(f.read(val_len).decode("utf-8"))


def main() -> None:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--ha-bluetooth")
    b.add_argument("--ha-rev", default="unknown")
    b.add_argument("--zha-root")
    b.add_argument("--zha-rev", default="unknown")
    b.add_argument("--oui-csv")
    b.add_argument("--oui-rev", default="unknown")
    b.add_argument("--db-version", type=int, default=1)
    b.add_argument("--build-epoch", type=int, default=0, help="set for reproducible test builds; 0 uses current time")
    b.add_argument("--out", required=True)
    v = sub.add_parser("validate")
    v.add_argument("db")
    v.add_argument("--release", action="store_true", help="also reject sources not cleared for redistribution")
    q = sub.add_parser("lookup")
    q.add_argument("db")
    q.add_argument("key")
    args = p.parse_args()
    if args.cmd == "build":
        build(args)
    elif args.cmd == "validate":
        result = validate_file(Path(args.db), release=args.release)
        print(json.dumps(result, indent=2, ensure_ascii=False))
        raise SystemExit(0 if result["ok"] else 2)
    else:
        result = lookup(Path(args.db), args.key)
        print(json.dumps(result, indent=2, ensure_ascii=False) if result is not None else "null")
        raise SystemExit(0 if result is not None else 1)


if __name__ == "__main__":
    main()
