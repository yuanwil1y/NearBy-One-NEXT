#!/usr/bin/env python3
"""Machine-facing pre-format validation for whole-SD recognition DB import."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import nearby_dbgen

INSTALL_MODEL = "whole_sd_destructive_v0_1"
ACTIVE_PATH = "/nearby/db/nearby.nbdb"
TEMPORARY_PATH = "/nearby/db/nearby.nbdb.part"
POST_FORMAT_FAILURE = "no_usable_db_guaranteed"


def preflight(path: Path, *, release: bool) -> dict:
    try:
        validation = nearby_dbgen.validate_file(path, release=release)
    except (OSError, ValueError, OverflowError) as exc:
        validation = {"ok": False, "errors": [str(exc)], "header": {}, "manifest": {}}

    ok = bool(validation.get("ok"))
    return {
        "ok": ok,
        "safe_to_format": ok,
        "policy_mode": "release" if release else "development",
        "install_model": INSTALL_MODEL,
        "destructive": True,
        "active_path": ACTIVE_PATH,
        "temporary_path": TEMPORARY_PATH,
        "post_format_failure": POST_FORMAT_FAILURE,
        "errors": list(validation.get("errors", [])),
        "validation": validation,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("db")
    parser.add_argument("--release", action="store_true")
    args = parser.parse_args()
    result = preflight(Path(args.db), release=args.release)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    raise SystemExit(0 if result["safe_to_format"] else 2)


if __name__ == "__main__":
    main()
