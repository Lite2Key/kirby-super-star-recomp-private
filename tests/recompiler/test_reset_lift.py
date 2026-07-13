import copy
import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.reset_lift import ResetLiftError, lift_reset_paths


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
