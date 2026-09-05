"""Conservative static extractor for zigbee-herdsman-converters device definitions.

No JavaScript/TypeScript is executed. Only literal recognition metadata is copied:
zigbeeModel strings, direct modelID/manufacturerName fingerprints, common
`tuya.fingerprint(model, manufacturers)` declarations, and literal product metadata.
Executable converters/configure/extend behavior remains firmware/Agent-B territory.
"""
from __future__ import annotations

import ast
from pathlib import Path
import re
from typing import Any, Iterator

_JS_STRING = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''


def _decode_string(token: str) -> str | None:
    try:
        value = ast.literal_eval(token)
    except (SyntaxError, ValueError):
        return None
    return value if isinstance(value, str) else None


def _strings(text: str) -> list[str]:
    out: list[str] = []
    for match in re.finditer(_JS_STRING, text):
        value = _decode_string(match.group(0))
        if value is not None:
            out.append(value)
    return out


def _prop_string(obj: str, name: str) -> str | None:
    match = re.search(rf"\b{re.escape(name)}\s*:\s*({_JS_STRING})", obj)
    return _decode_string(match.group(1)) if match else None


def _definition_objects(text: str) -> Iterator[str]:
    marker = text.find("export const definitions")
    if marker < 0:
        return
    array_start = text.find("[", marker)
    if array_start < 0:
        return

    brace_depth = 0
    start = -1
    quote: str | None = None
    escaped = False
    line_comment = False
    block_comment = False
    i = array_start + 1
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if ch == "\n":
                line_comment = False
            i += 1
            continue
        if block_comment:
            if ch == "*" and nxt == "/":
                block_comment = False
                i += 2
            else:
                i += 1
            continue
        if quote is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            i += 1
            continue
        if ch == "/" and nxt == "/":
            line_comment = True
            i += 2
            continue
        if ch == "/" and nxt == "*":
            block_comment = True
            i += 2
            continue
        if ch in {'"', "'", "`"}:
            quote = ch
            i += 1
            continue
        if ch == "{":
            if brace_depth == 0:
                start = i
            brace_depth += 1
        elif ch == "}" and brace_depth:
            brace_depth -= 1
            if brace_depth == 0 and start >= 0:
                yield text[start : i + 1]
                start = -1
        elif ch == "]" and brace_depth == 0:
            return
        i += 1


def _zigbee_models(obj: str) -> list[str]:
    match = re.search(r"\bzigbeeModel\s*:\s*\[(.*?)\]", obj, flags=re.S)
    return _strings(match.group(1)) if match else []


def _direct_fingerprints(obj: str) -> list[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    for chunk_match in re.finditer(r"\{[^{}]{0,1800}\}", obj, flags=re.S):
        chunk = chunk_match.group(0)
        model = _prop_string(chunk, "modelID")
        manufacturer = _prop_string(chunk, "manufacturerName")
        if model and manufacturer:
            pairs.add((manufacturer, model))
    return sorted(pairs)


def _tuya_fingerprints(obj: str) -> list[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    pattern = re.compile(rf"tuya\.fingerprint\(\s*({_JS_STRING})\s*,\s*\[(.*?)\]\s*\)", flags=re.S)
    for match in pattern.finditer(obj):
        model = _decode_string(match.group(1))
        if not model:
            continue
        for manufacturer in _strings(match.group(2)):
            pairs.add((manufacturer, model))
    return sorted(pairs)


def _base(revision: str, source_path: str, product_model: str | None, vendor: str | None,
          description: str | None) -> dict[str, Any]:
    out: dict[str, Any] = {
        "source_id": "z2m_converters",
        "source_revision": revision,
        "source_path": source_path,
        "classification": "DATA_ONLY",
        "source_classification": "DATA+PARSER",
    }
    if product_model:
        out["product_model"] = product_model
    if vendor:
        out["vendor"] = vendor
    if description:
        out["description"] = description
    return out


def extract_z2m(root: Path, revision: str) -> tuple[list[dict[str, Any]], dict[str, int]]:
    records: list[dict[str, Any]] = []
    stats = {
        "typescript_files": 0,
        "definition_objects": 0,
        "definitions_with_static_identity": 0,
        "zigbee_model_records": 0,
        "literal_fingerprint_records": 0,
        "tuya_fingerprint_records": 0,
        "dynamic_fingerprint_fields_unresolved": 0,
    }
    for path in sorted(root.rglob("*.ts")):
        stats["typescript_files"] += 1
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        rel = path.relative_to(root).as_posix()
        for obj in _definition_objects(text):
            stats["definition_objects"] += 1
            product_model = _prop_string(obj, "model")
            vendor = _prop_string(obj, "vendor")
            description = _prop_string(obj, "description")
            zigbee_models = _zigbee_models(obj)
            direct = _direct_fingerprints(obj)
            tuya = _tuya_fingerprints(obj)
            if "fingerprint" in obj and not direct and not tuya:
                stats["dynamic_fingerprint_fields_unresolved"] += 1
            if zigbee_models or direct or tuya:
                stats["definitions_with_static_identity"] += 1
            for model_id in zigbee_models:
                rec = _base(revision, rel, product_model, vendor, description)
                rec.update(
                    key=f"zigbee:model:{model_id}",
                    record_type="z2m_zigbee_model",
                    zigbee_model=model_id,
                )
                records.append(rec)
                stats["zigbee_model_records"] += 1
            for manufacturer, model_id in direct:
                rec = _base(revision, rel, product_model, vendor, description)
                rec.update(
                    key=f"zigbee:fingerprint:{manufacturer}\x1f{model_id}",
                    record_type="z2m_fingerprint",
                    manufacturer=manufacturer,
                    model=model_id,
                    fingerprint_kind="literal",
                )
                records.append(rec)
                stats["literal_fingerprint_records"] += 1
            for manufacturer, model_id in tuya:
                rec = _base(revision, rel, product_model, vendor, description)
                rec.update(
                    key=f"zigbee:fingerprint:{manufacturer}\x1f{model_id}",
                    record_type="z2m_fingerprint",
                    manufacturer=manufacturer,
                    model=model_id,
                    fingerprint_kind="tuya_helper_literal_args",
                )
                records.append(rec)
                stats["tuya_fingerprint_records"] += 1
    return records, stats
