import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.reset_lift import lift_reset_paths
from recompiler.kssrecomp.wram_witness import WramWitnessError, parse_witness_lines

from test_reset_lift import identity, inputs


ROOT = Path(__file__).resolve().parents[2]


def test_parser_is_sparse_deterministic_and_schema_validates() -> None:
    artifact = parse_witness_lines([
        "noise",
        "KSS_WRAM_BYTES_V1|000011|A90F002C",
        "KSS_WRAM_BYTES_V1|00000E|9C0E30A9",
        "KSS_WRAM_BYTES_V1|000011|A90F002C",
    ])
    assert [block["pc"] for block in artifact["blocks"]] == [0x0E, 0x11]
    assert artifact["blocks"][0]["mode"] == {
        "emulation": False, "m8": False, "x8": False,
    }
    schema = json.loads((ROOT / "schemas/recompiler/wram-witness.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(artifact)


def test_parser_rejects_conflicting_or_out_of_range_markers() -> None:
    with pytest.raises(WramWitnessError, match="contradictory"):
        parse_witness_lines([
            "KSS_WRAM_BYTES_V1|00000E|9C0E30A9",
            "KSS_WRAM_BYTES_V1|00000E|EA000000",
        ])
    with pytest.raises(WramWitnessError, match="below"):
        parse_witness_lines(["KSS_WRAM_BYTES_V1|002000|EA000000"])


def test_lifter_decodes_non_rom_scpu_nodes_only_with_private_witness() -> None:
    vectors, cfg, rom = inputs()
    low0 = identity("scpu", 0x00000E, False, False, False)
    low1 = identity("scpu", 0x000011, False, False, False)
    low2 = identity("scpu", 0x000014, False, False, False)
    cfg_blocks = cfg["cfg"]["blocks"]
    cfg_blocks.extend(
        {"identity": node, "instructions": [], "edges": []}
        for node in [low0, low1, low2]
    )
    by_identity = {block["identity"]["pc"]: block for block in cfg_blocks}
    by_identity[0x00000E]["edges"] = [{
        "kind": "observed", "source": low0, "target": low1, "evidence_id": "wram-0e-11",
    }]
    by_identity[0x000011]["edges"] = [{
        "kind": "observed", "source": low1, "target": low2, "evidence_id": "wram-11-14",
    }]
    artifact = parse_witness_lines([
        "KSS_WRAM_BYTES_V1|00000E|9C0E30A9",
        "KSS_WRAM_BYTES_V1|000011|A90F002C",
        "KSS_WRAM_BYTES_V1|000014|EA000000",
    ])
    result = lift_reset_paths(
        vectors, cfg, rom,
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
        max_blocks_per_processor=4096,
        wram_witness=artifact,
    )
    blocks = {block["identity"]["pc"]: block for block in result["blocks"]}
    assert blocks[0x00000E]["status"] == "decoded"
    assert blocks[0x00000E]["memory_region"] == "wram"
    assert blocks[0x00000E]["instruction"]["bytes_hex"] == "9C0E30"
    assert blocks[0x000011]["instruction"]["mnemonic"] == "LDA"
    assert result["source"]["wram_witness_policy"] == "private-sparse-bytes-v1"

    schema = json.loads((ROOT / "schemas/recompiler/lifted-reset-cfg.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(result)


def test_wram_rti_uses_observed_restored_modes_without_rom_offset() -> None:
    vectors, cfg, rom = inputs()
    source = identity("scpu", 0x000040, False, False, False)
    targets = [
        identity("scpu", 0x008200, False, False, False),
        identity("scpu", 0x008201, False, False, True),
    ]
    cfg_blocks = cfg["cfg"]["blocks"]
    cfg_blocks.extend(
        {"identity": node, "instructions": [], "edges": []}
        for node in [source, *targets]
    )
    source_block = next(block for block in cfg_blocks if block["identity"] == source)
    source_block["edges"] = [
        {"kind": "observed", "source": source, "target": target, "evidence_id": "wram-rti"}
        for target in targets
    ]
    rom_bytes = bytearray(rom)
    rom_bytes[0x200] = 0xEA
    rom_bytes[0x201] = 0xEA
    witness = parse_witness_lines(["KSS_WRAM_BYTES_V1|000040|40000000"])
    result = lift_reset_paths(
        vectors, cfg, bytes(rom_bytes),
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
        max_blocks_per_processor=4096,
        wram_witness=witness,
    )
    rti = next(block for block in result["blocks"] if block["identity"] == source)
    assert rti["status"] == "decoded"
    assert rti["memory_region"] == "wram"
    assert rti["instruction"]["mnemonic"] == "RTI"
    assert rti["restored_modes"] == [target["mode"] for target in targets]
