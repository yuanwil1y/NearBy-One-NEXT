"""Mechanical extractors for Home Assistant generated LAN discovery tables.

The functions consume already-generated Python literals. They never import Home
Assistant and never parse network packets; Agent B supplies normalized observations.
"""
from __future__ import annotations

import ast
import hashlib
from pathlib import Path
from typing import Any

import nearby_dbgen


def _assignment_literal(path: Path, name: str) -> Any:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        value = None
        if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name) and node.target.id == name:
            value = node.value
        elif isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            value = node.value
        if value is not None:
            try:
                return ast.literal_eval(value)
            except (ValueError, TypeError, SyntaxError) as exc:
                raise ValueError(f"{name} is not a pure generated literal in {path}: {exc}") from exc
    raise ValueError(f"{name} assignment not found in {path}")


def _digest(value: Any) -> str:
    return hashlib.sha256(nearby_dbgen.canonical_json(value)).hexdigest()[:16]


def _common(record_type: str, revision: str) -> dict[str, Any]:
    return {
        "record_type": record_type,
        "source_id": "ha_core",
        "source_revision": revision,
        "classification": "DATA_ONLY",
    }


def extract_ha_zeroconf(path: Path, revision: str) -> list[dict[str, Any]]:
    raw = _assignment_literal(path, "ZEROCONF")
    if not isinstance(raw, dict):
        raise ValueError("ZEROCONF must be a dict")
    out: list[dict[str, Any]] = []
    for service_type in sorted(raw):
        matchers = raw[service_type]
        if not isinstance(service_type, str) or not isinstance(matchers, list):
            continue
        for matcher in matchers:
            if not isinstance(matcher, dict) or not isinstance(matcher.get("domain"), str):
                continue
            domain = matcher["domain"]
            rec = _common("ha_zeroconf_matcher", revision)
            rec.update(
                key=f"lan:zeroconf:{service_type}\x1f{domain}\x1f{_digest(matcher)}",
                service_type=service_type,
                domain=domain,
                matcher=matcher,
            )
            out.append(rec)
    return out


def _ssdp_anchor(matcher: dict[str, Any]) -> tuple[str, str]:
    for field in ("st", "deviceType", "manufacturer", "manufacturerURL"):
        value = matcher.get(field)
        if isinstance(value, str) and value:
            return field, value
    return "any", "*"


def extract_ha_ssdp(path: Path, revision: str) -> list[dict[str, Any]]:
    raw = _assignment_literal(path, "SSDP")
    if not isinstance(raw, dict):
        raise ValueError("SSDP must be a dict")
    out: list[dict[str, Any]] = []
    for domain in sorted(raw):
        matchers = raw[domain]
        if not isinstance(domain, str) or not isinstance(matchers, list):
            continue
        for matcher in matchers:
            if not isinstance(matcher, dict):
                continue
            anchor_field, anchor_value = _ssdp_anchor(matcher)
            rec = _common("ha_ssdp_matcher", revision)
            rec.update(
                key=f"lan:ssdp:{anchor_field}:{anchor_value}\x1f{domain}\x1f{_digest(matcher)}",
                domain=domain,
                anchor_field=anchor_field,
                anchor_value=anchor_value,
                matcher=matcher,
            )
            out.append(rec)
    return out


def _dhcp_anchor(matcher: dict[str, Any]) -> tuple[str, str]:
    mac = matcher.get("macaddress")
    if isinstance(mac, str) and mac:
        return "mac", mac.upper()
    hostname = matcher.get("hostname")
    if isinstance(hostname, str) and hostname:
        return "hostname", hostname.lower()
    if matcher.get("registered_devices") is True:
        return "registered", "1"
    return "any", "*"


def extract_ha_dhcp(path: Path, revision: str) -> list[dict[str, Any]]:
    raw = _assignment_literal(path, "DHCP")
    if not isinstance(raw, list):
        raise ValueError("DHCP must be a list")
    out: list[dict[str, Any]] = []
    for matcher in raw:
        if not isinstance(matcher, dict) or not isinstance(matcher.get("domain"), str):
            continue
        domain = matcher["domain"]
        anchor_field, anchor_value = _dhcp_anchor(matcher)
        rec = _common("ha_dhcp_matcher", revision)
        rec.update(
            key=f"lan:dhcp:{anchor_field}:{anchor_value}\x1f{domain}\x1f{_digest(matcher)}",
            domain=domain,
            anchor_field=anchor_field,
            anchor_value=anchor_value,
            matcher=matcher,
        )
        out.append(rec)
    return out
