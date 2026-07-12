"""Validate that a Ghidra export has the strict ROM-free metadata shape."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any
from jsonschema import Draft202012Validator

FORBIDDEN_KEYS = frozenset({"bytes", "data", "disassembly", "instruction", "string", "strings", "asset", "assets"})


def _walk(value: Any, path: str = "$") -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key.casefold() in FORBIDDEN_KEYS:
                raise ValueError(f"forbidden protected-content field at {path}.{key}")
            _walk(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _walk(child, f"{path}[{index}]")


def validate_export(document: Any, schema: Any) -> None:
    Draft202012Validator.check_schema(schema)
    errors = sorted(Draft202012Validator(schema).iter_errors(document), key=lambda error: list(error.path))
    if errors:
        locations = "; ".join(f"$.{'.'.join(map(str, error.path))}: {error.message}" for error in errors)
        raise ValueError(locations)
    _walk(document)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export", type=Path)
    parser.add_argument("--schema", type=Path, required=True)
    args = parser.parse_args()
    document = json.loads(args.export.read_text(encoding="utf-8"))
    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    validate_export(document, schema)
    print(f"valid ROM-free Ghidra export: {args.export}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
