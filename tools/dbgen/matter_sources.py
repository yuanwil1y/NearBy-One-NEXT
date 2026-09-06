"""Conservative static extractor for SmartThings Matter VID/PID fingerprints.

The source repository defines a strict fingerprints.yml shape. This parser reads only
the top-level `matterManufacturer` section and its literal scalar fields; it is not a
general YAML parser and never executes SmartThings/Lua code.
"""
from __future__ import annotations

import ast
from pathlib import Path
import re
from typing import Any

_SECTION = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*:\s*(?:#.*)?$")
_ENTRY = re.compile(r"^  - id:\s*(.*?)\s*$")
_FIELD = re.compile(r"^    ([A-Za-z][A-Za-z0-9_-]*):\s*(.*?)\s*$")


def _scalar(text: str) -> str:
    text = text.strip()
    if not text:
        return ""
    if text[0:1] in {'\"', "'"}:
        try:
            value = ast.literal_eval(text)
        except (SyntaxError, ValueError):
            return text
        return value if isinstance(value, str) else str(value)
    return text.split(" #", 1)[0].strip()


def _uint16(text: str) -> int | None:
    try:
        value = int(_scalar(text), 0)
    except ValueError:
        return None
    return value if 0 <= value <= 0xFFFF else None


def _emit(fields: dict[str, str], revision: str, source_path: str) -> dict[str, Any] | None:
    vendor_id = _uint16(fields.get("vendorId", ""))
    product_id = _uint16(fields.get("productId", ""))
    if vendor_id is None or product_id is None:
        return None
    record: dict[str, Any] = {
        "key": f"matter:vidpid:{vendor_id:04x}:{product_id:04x}",
        "record_type": "matter_vid_pid_fingerprint",
        "vendor_id": vendor_id,
        "product_id": product_id,
        "source_id": "smartthings_matter_fingerprints",
        "source_revision": revision,
        "source_path": source_path,
        "classification": "DATA_ONLY",
        "catalog_semantics": "integration_fingerprint_not_authoritative_assignment_registry",
    }
    for source_name, target_name in (
        ("id", "fingerprint_id"),
        ("deviceLabel", "device_label"),
        ("deviceProfileName", "device_profile_name"),
    ):
        value = _scalar(fields.get(source_name, ""))
        if value:
            record[target_name] = value
    return record


def extract_file(path: Path, revision: str, source_path: str | None = None) -> list[dict[str, Any]]:
    rel = source_path or path.as_posix()
    records: list[dict[str, Any]] = []
    in_matter = False
    fields: dict[str, str] | None = None

    def finish() -> None:
        nonlocal fields
        if fields is None:
            return
        record = _emit(fields, revision, rel)
        if record is not None:
            records.append(record)
        fields = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if len(line) == len(line.lstrip()) and _SECTION.match(line):
            finish()
            in_matter = line.split(":", 1)[0] == "matterManufacturer"
            continue
        if not in_matter:
            continue
        entry = _ENTRY.match(line)
        if entry:
            finish()
            fields = {"id": entry.group(1)}
            continue
        field = _FIELD.match(line)
        if field and fields is not None:
            fields[field.group(1)] = field.group(2)
    finish()
    return records


def extract_tree(root: Path, revision: str) -> tuple[list[dict[str, Any]], dict[str, int]]:
    records: list[dict[str, Any]] = []
    files_scanned = 0
    files_with_matter = 0
    for path in sorted(root.rglob("fingerprints.yml")):
        files_scanned += 1
        rel = path.relative_to(root).as_posix()
        found = extract_file(path, revision, rel)
        if found:
            files_with_matter += 1
            records.extend(found)
    return records, {
        "fingerprint_files_scanned": files_scanned,
        "fingerprint_files_with_matter_manufacturer": files_with_matter,
        "matter_vid_pid_records": len(records),
    }
