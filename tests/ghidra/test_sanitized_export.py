import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "schemas" / "ghidra" / "sanitized-program.schema.json"
VALIDATOR_PATH = ROOT / "tools" / "ghidra" / "validate_export.py"
SPEC = importlib.util.spec_from_file_location("ghidra_validate_export", VALIDATOR_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


@pytest.fixture
def schema():
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


@pytest.fixture
def synthetic_export():
    def address(offset):
        return {"space": "SNES", "offset": offset}

    return {
        "schema_version": 1,
        "program_id": "synthetic-program-id",
        "language_id": "65816:LE:24:default",
        "compiler_spec_id": "default",
        "image_base": address("0x8000"),
        "memory_blocks": [{"start": address("0x8000"), "end": address("0xFFFF"),
                           "permissions": {"read": True, "write": False, "execute": True}}],
        "entry_points": [address("0xFFFC")],
        "functions": [{"entry": address("0x8000"), "name": "reset"}],
        "symbols": [{"address": address("0x8000"), "type": "Function"}],
    }


def test_synthetic_export_is_valid(schema, synthetic_export):
    MODULE.validate_export(synthetic_export, schema)


@pytest.mark.parametrize("forbidden", ["bytes", "disassembly", "strings", "assets"])
def test_protected_content_fields_are_rejected(schema, synthetic_export, forbidden):
    synthetic_export["functions"][0][forbidden] = "protected content"
    with pytest.raises(ValueError, match="forbidden protected-content field|Additional properties"):
        MODULE.validate_export(synthetic_export, schema)


def test_symbol_names_are_not_permitted(schema, synthetic_export):
    synthetic_export["symbols"][0]["name"] = "potentially_string_derived_label"
    with pytest.raises(ValueError, match="Additional properties"):
        MODULE.validate_export(synthetic_export, schema)


def test_raw_numeric_addresses_are_rejected(schema, synthetic_export):
    synthetic_export["entry_points"][0] = 0xFFFC
    with pytest.raises(ValueError, match="not of type 'object'"):
        MODULE.validate_export(synthetic_export, schema)
