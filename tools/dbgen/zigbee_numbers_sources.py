"""Static Zigbee assigned identifiers from pinned zigbee-herdsman definitions.

This is a PC-side text extractor. It does not import/execute TypeScript and only
copies literal numeric constants from the pinned MIT-licensed source files.
"""
from __future__ import annotations

from pathlib import Path
import re
from typing import Any

_ENUM_START = re.compile(r"\bexport\s+enum\s+ManufacturerCode\s*\{")
_ENUM_ITEM = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,?\s*(?://.*)?$")
_PROFILE_CONST = re.compile(
    r"^\s*export\s+const\s+(HA_PROFILE_ID|SE_PROFILE_ID|GP_PROFILE_ID|TOUCHLINK_PROFILE_ID)\s*=\s*"
    r"(0x[0-9a-fA-F]+|\d+)\s*;"
)


def _parse_int(text: str) -> int:
    return int(text, 0)


def extract_manufacturer_codes(path: Path, revision: str) -> list[dict[str, Any]]:
    text = path.read_text(encoding="utf-8")
    start = _ENUM_START.search(text)
    if start is None:
        raise ValueError(f"ManufacturerCode enum not found in {path}")
    body_start = start.end()
    body_end = text.find("}", body_start)
    if body_end < 0:
        raise ValueError(f"unterminated ManufacturerCode enum in {path}")

    records: list[dict[str, Any]] = []
    for line in text[body_start:body_end].splitlines():
        match = _ENUM_ITEM.match(line)
        if match is None:
            continue
        enum_name, raw_code = match.groups()
        code = _parse_int(raw_code)
        if not 0 <= code <= 0xFFFF:
            continue
        records.append(
            {
                "key": f"zigbee:assigned:manufacturer:{code:04x}",
                "record_type": "zigbee_manufacturer_code",
                "manufacturer_code": code,
                "enum_name": enum_name,
                "source_id": "zigbee_herdsman_defs",
                "source_revision": revision,
                "source_path": "src/zspec/zcl/definition/manufacturerCode.ts",
                "classification": "DATA_ONLY",
            }
        )
    if not records:
        raise ValueError(f"ManufacturerCode enum contained no literal records in {path}")
    return records


def extract_profile_ids(path: Path, revision: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _PROFILE_CONST.match(line)
        if match is None:
            continue
        constant_name, raw_id = match.groups()
        profile_id = _parse_int(raw_id)
        if not 0 <= profile_id <= 0xFFFF:
            continue
        records.append(
            {
                "key": f"zigbee:assigned:profile:{profile_id:04x}",
                "record_type": "zigbee_profile_id",
                "profile_id": profile_id,
                "constant_name": constant_name,
                "source_id": "zigbee_herdsman_defs",
                "source_revision": revision,
                "source_path": "src/zspec/consts.ts",
                "classification": "DATA_ONLY",
            }
        )
    if not records:
        raise ValueError(f"no supported literal Zigbee profile IDs found in {path}")
    return records
