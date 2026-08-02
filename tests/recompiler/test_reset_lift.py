import copy
import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.reset_lift import ResetLiftError, _rom_offset, lift_reset_paths
from recompiler.kssrecomp.generated_blocks import render as render_generated_blocks


ROOT = Path(__file__).resolve().parents[2]


def identity(processor: str, pc: int, e: bool, m: bool, x: bool) -> dict[str, object]:
    return {"processor": processor, "pc": pc, "mode": {"emulation": e, "m8": m, "x8": x}}


def inputs() -> tuple[dict[str, object], dict[str, object], bytes]:
    s0 = identity("scpu", 0x8000, True, True, True)
    s1 = identity("scpu", 0x8001, True, True, True)
    s2 = identity("scpu", 0x8002, False, True, True)
    s4 = identity("scpu", 0x8004, False, False, False)
    a0 = identity("sa1", 0x8100, True, True, True)
    a1 = identity("sa1", 0x8101, True, True, True)
    edges = [(s0, s1), (s1, s2), (s2, s4), (a0, a1)]
    by_source: dict[tuple[str, int], list[dict[str, object]]] = {}
    for source, target in edges:
        by_source.setdefault((source["processor"], source["pc"]), []).append({
            "kind": "observed", "source": source, "target": target, "evidence_id": "synthetic"
        })
    nodes = [s0, s1, s2, s4, a0, a1]
    cfg = {
        "schema_version": 1,
        "entries": [
            {"kind": "reset_trace_seed", "identity": s0, "first_cycle": 0},
            {"kind": "reset_trace_seed", "identity": a0, "first_cycle": 5},
        ],
        "cfg": {"schema_version": 1, "blocks": [
            {"identity": node, "instructions": [], "edges": by_source.get((node["processor"], node["pc"]), [])}
            for node in nodes
        ]},
    }
    vectors = {
        "schema_version": 1,
        "cartridge": {"mapping": "sa1-lorom", "payload_size": 32768, "copier_header": False},
        "processors": {
            "scpu": {"vectors": [{"name": "emulation_reset", "target_cpu_address": 0x8000}]},
            "sa1": {"observed_reset_entry": {"target_cpu_address": 0x8100}},
        },
    }
    rom = bytearray(32768)
    rom[0:5] = bytes([0x18, 0xFB, 0xC2, 0x30, 0xEA])  # CLC; XCE; REP #$30; NOP
    rom[0x100:0x104] = bytes([0xEA, 0x6C, 0x00, 0x90])  # NOP; JMP ($9000)
    return vectors, cfg, bytes(rom)


def test_lifts_mode_transition_and_stops_at_dynamic_control_flow() -> None:
    vectors, cfg, rom = inputs()
    result = lift_reset_paths(vectors, cfg, rom)
    blocks = {(b["identity"]["processor"], b["identity"]["pc"]): b for b in result["blocks"]}
    assert blocks[("scpu", 0x8001)]["instruction"]["mnemonic"] == "XCE"
    assert blocks[("scpu", 0x8002)]["instruction"]["bytes_hex"] == "C230"
    assert blocks[("sa1", 0x8101)]["status"] == "unresolved"
    assert blocks[("sa1", 0x8101)]["unresolved_reason"] == "unsupported_dynamic_control_flow"
    assert result["regions"]["scpu"]["decoded_blocks"] == 4


def test_route_lift_analyzes_all_observed_nodes_and_accepts_observed_dynamic_flow() -> None:
    vectors, cfg, rom = inputs()
    result = lift_reset_paths(
        vectors, cfg, rom,
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
    )
    assert len(result["blocks"]) == 6
    assert all(block["status"] == "decoded" for block in result["blocks"])
    assert result["source"]["dynamic_control_flow_policy"] == "observed-successors-fail-closed"
    assert result["source"]["selection_policy"] == "all-observed-decoded"


def test_route_lift_represents_polymorphic_rti_modes_and_keeps_successors_bounded() -> None:
    vectors, cfg, rom = inputs()
    source = identity("scpu", 0x84A1, False, False, False)
    targets = [
        identity("scpu", 0x8200, False, False, False),
        identity("scpu", 0x8201, False, False, True),
        identity("scpu", 0x8202, False, True, False),
    ]
    rom_bytes = bytearray(rom)
    # $0084A1 is RTI; use harmless leaves for the observed return identities.
    rom_bytes[0x4A1] = 0x40
    for offset in (0x200, 0x201, 0x202):
        rom_bytes[offset] = 0xEA
    cfg_blocks = cfg["cfg"]["blocks"]
    cfg_blocks.extend(
        {"identity": node, "instructions": [], "edges": []}
        for node in [source, *targets]
    )
    source_block = next(block for block in cfg_blocks if block["identity"] == source)
    source_block["edges"] = [
        {"kind": "observed", "source": source, "target": target, "evidence_id": "synthetic-rti"}
        for target in targets
    ]

    result = lift_reset_paths(
        vectors,
        cfg,
        bytes(rom_bytes),
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
        max_blocks_per_processor=4096,
    )
    rti = next(block for block in result["blocks"] if block["identity"] == source)
    assert rti["status"] == "decoded"
    assert rti["instruction"]["mnemonic"] == "RTI"
    assert rti["restored_modes"] == [target["mode"] for target in targets]
    rti_edges = [edge for edge in result["edges"] if edge["source"] == source]
    assert {edge["target"]["pc"] for edge in rti_edges} == {0x8200, 0x8201, 0x8202}

    # The generated dispatcher registers these observed identities; a
    # different stack-restored PC or mode reaches the normal unknown-block
    # stop path instead of inheriting one arbitrary restored mode.
    source_text = render_generated_blocks(
        result, max_blocks_per_processor=4096,
        registration_name="polymorphic_rti",
    )[1]
    assert "0x008200U" in source_text
    assert "0x008201U" in source_text
    assert "0x008202U" in source_text


def test_sa1_linear_high_bank_rom_mapping() -> None:
    assert _rom_offset(0xC1ABCD, 0x400000) == 0x01ABCD
    assert _rom_offset(0x008000, 0x400000) == 0


def test_unknown_carry_stops_xce_without_guessing() -> None:
    vectors, cfg, rom = inputs()
    cfg["entries"][0]["identity"] = cfg["cfg"]["blocks"][1]["identity"]
    vectors["processors"]["scpu"]["vectors"][0]["target_cpu_address"] = 0x8001
    result = lift_reset_paths(vectors, cfg, rom)
    scpu = next(block for block in result["blocks"] if block["identity"]["processor"] == "scpu")
    assert scpu["status"] == "unresolved"
    assert scpu["unresolved_reason"].startswith("ambiguous_mode:XCE")


def test_rejects_vector_trace_disagreement() -> None:
    vectors, cfg, rom = inputs()
    vectors["processors"]["sa1"]["observed_reset_entry"]["target_cpu_address"] += 1
    with pytest.raises(ResetLiftError, match="SA-1"):
        lift_reset_paths(vectors, cfg, rom)


def test_synthetic_output_validates_and_has_bounded_instruction_bytes() -> None:
    vectors, cfg, rom = inputs()
    result = lift_reset_paths(vectors, cfg, rom)
    schema = json.loads((ROOT / "schemas/recompiler/lifted-reset-cfg.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(result)
    byte_fields = [b["instruction"]["bytes_hex"] for b in result["blocks"] if b["instruction"]]
    assert byte_fields and all(2 <= len(value) <= 8 for value in byte_fields)
    assert all(set(value) <= set("0123456789ABCDEF") for value in byte_fields)


def test_output_is_deterministic() -> None:
    vectors, cfg, rom = inputs()
    assert lift_reset_paths(vectors, cfg, rom) == lift_reset_paths(
        copy.deepcopy(vectors), copy.deepcopy(cfg), bytes(rom)
    )


def test_lift_preserves_identity_route_provenance() -> None:
    vectors, cfg, rom = inputs()
    cfg["observations"] = {"blocks": [
        {"identity": cfg["cfg"]["blocks"][0]["identity"], "routes": ["boot", "first-visible"]}
    ]}
    result = lift_reset_paths(vectors, cfg, rom)
    first = next(block for block in result["blocks"] if block["identity"]["pc"] == 0x8000)
    assert first["routes"] == ["boot", "first-visible"]
    schema = json.loads((ROOT / "schemas/recompiler/lifted-reset-cfg.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(result)
