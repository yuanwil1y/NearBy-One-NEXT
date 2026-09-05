"""Static Bluetooth assigned-number metadata from a pinned redistributable mirror."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

BT_BASE_SUFFIX = "-0000-1000-8000-00805f9b34fb"


def _load_list(path: Path) -> list[dict[str, Any]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, list):
        raise ValueError(f"expected JSON list in {path}")
    return [item for item in raw if isinstance(item, dict)]


def canonical_bluetooth_uuid(value: str) -> str:
    text = value.strip().lower()
    if len(text) == 4 and all(ch in "0123456789abcdef" for ch in text):
        return f"0000{text}{BT_BASE_SUFFIX}"
    if len(text) == 8 and all(ch in "0123456789abcdef" for ch in text):
        return f"{text}{BT_BASE_SUFFIX}"
    if len(text) == 36 and text.count("-") == 4:
        return text
    raise ValueError(f"unsupported Bluetooth UUID: {value!r}")


def extract_company_ids(path: Path, revision: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for item in _load_list(path):
        code = item.get("code")
        name = item.get("name")
        if not isinstance(code, int) or not 0 <= code <= 0xFFFF or not isinstance(name, str) or not name:
            continue
        records.append(
            {
                "key": f"ble:assigned:company:{code:04x}",
                "record_type": "bluetooth_company_id",
                "company_id": code,
                "name": name,
                "source_id": "bluetooth_numbers_nordic",
                "source_revision": revision,
                "classification": "DATA_ONLY",
            }
        )
    return records


def extract_service_uuids(path: Path, revision: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for item in _load_list(path):
        raw_uuid = item.get("uuid")
        name = item.get("name")
        if not isinstance(raw_uuid, str) or not isinstance(name, str) or not name:
            continue
        try:
            uuid = canonical_bluetooth_uuid(raw_uuid)
        except ValueError:
            continue
        record: dict[str, Any] = {
            "key": f"ble:assigned:service:{uuid}",
            "record_type": "bluetooth_service_uuid",
            "uuid": uuid,
            "assigned_uuid": raw_uuid,
            "name": name,
            "source_id": "bluetooth_numbers_nordic",
            "source_revision": revision,
            "classification": "DATA_ONLY",
        }
        if isinstance(item.get("identifier"), str):
            record["identifier"] = item["identifier"].strip()
        if isinstance(item.get("source"), str):
            record["upstream_category"] = item["source"]
        records.append(record)
    return records
