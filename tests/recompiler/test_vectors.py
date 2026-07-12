import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.errors import RomFormatError
from recompiler.kssrecomp.vectors import extract_vectors, observed_processor_entry, observed_sa1_reset


ROOT = Path(__file__).resolve().parents[2]


def synthetic_sa1_lorom() -> bytes:
    rom = bytearray(0x10000)
    base = 0x7FC0
    rom[base : base + 21] = b"VECTOR TEST".ljust(21, b" ")
    rom[base + 0x15] = 0x23
    rom[base + 0x16] = 0x35
    rom[base + 0x1C : base + 0x1E] = (0xEDCB).to_bytes(2, "little")
    rom[base + 0x1E : base + 0x20] = (0x1234).to_bytes(2, "little")
    for index, offset in enumerate((0x24, 0x26, 0x28, 0x2A, 0x2E, 0x34, 0x38, 0x3A, 0x3C, 0x3E)):
        rom[base + offset : base + offset + 2] = (0x8000 + index * 2).to_bytes(2, "little")
    return bytes(rom)


def test_extracts_only_address_and_mapping_metadata() -> None:
    result = extract_vectors(synthetic_sa1_lorom(), sa1_reset_pc=0x008BF4)
    schema = json.loads((ROOT / "schemas/recompiler/reset-vectors.schema.json").read_text())
    jsonschema.validate(result, schema)
    assert result["cartridge"]["mapping"] == "sa1-lorom"
    reset = next(v for v in result["processors"]["scpu"]["vectors"] if v["name"] == "emulation_reset")
    assert reset["vector_cpu_address"] == 0x00FFFC
    assert reset["target_cpu_address"] == 0x8010
    assert result["processors"]["sa1"]["observed_reset_entry"]["target_cpu_address"] == 0x008BF4
    serialized = json.dumps(result)
    for forbidden in ("title", "bytes", "disassembly", "string", "rom_path"):
        assert forbidden not in serialized.lower()


def test_sa1_observed_entry_uses_earliest_cycle_not_json_order() -> None:
    trace = {"blocks": [
        {"processor": "sa1", "pc": 0x9000, "first_cycle": 5},
        {"processor": "scpu", "pc": 0x8004, "first_cycle": 0},
        {"processor": "sa1", "pc": 0x8BF4, "first_cycle": 1},
    ]}
    assert observed_sa1_reset(trace) == 0x8BF4


def test_sa1_observed_entry_rejects_missing_evidence() -> None:
    with pytest.raises(RomFormatError, match="no SA-1"):
        observed_sa1_reset({"blocks": []})


def test_uninstalled_low_vector_has_no_rom_offset() -> None:
    rom = bytearray(synthetic_sa1_lorom())
    rom[0x7FC0 + 0x24 : 0x7FC0 + 0x26] = bytes(2)
    result = extract_vectors(bytes(rom))
    cop = result["processors"]["scpu"]["vectors"][0]
    assert cop["target_cpu_address"] == 0
    assert cop["target_rom_offset"] is None


def test_scpu_observed_entry_uses_scpu_blocks_only() -> None:
    trace = {"blocks": [
        {"processor": "sa1", "pc": 0x8BF4, "first_cycle": 0},
        {"processor": "scpu", "pc": 0x8004, "first_cycle": 3},
    ]}
    assert observed_processor_entry(trace, "scpu") == 0x8004
