import json
from pathlib import Path

import jsonschema

from recompiler.kssrecomp.reset_lift import lift_reset_paths


ROOT = Path(__file__).resolve().parents[2]


def identity(processor: str, pc: int, e: bool, m: bool, x: bool) -> dict[str, object]:
    return {"processor": processor, "pc": pc, "mode": {"emulation": e, "m8": m, "x8": x}}


def inputs(*, direct_jump: bool = False) -> tuple[dict[str, object], dict[str, object], bytes]:
    reset = identity("scpu", 0x8000, False, False, False)
    normal = identity("scpu", 0x8001, False, False, False)
    nmi = identity("scpu", 0x8200, False, False, False)
    sa1 = identity("sa1", 0x8100, False, False, False)
    source_edges = [
        {"kind": "observed", "source": reset,
         "target": nmi if direct_jump else normal, "evidence_id": "normal"},
    ]
    if not direct_jump:
        source_edges.append({
            "kind": "observed", "source": reset, "target": nmi, "evidence_id": "nmi",
        })
    blocks = [
        {"identity": reset, "instructions": [], "edges": source_edges},
        {"identity": normal, "instructions": [], "edges": []},
        {"identity": nmi, "instructions": [], "edges": []},
        {"identity": sa1, "instructions": [], "edges": []},
    ]
    cfg = {
        "schema_version": 1,
        "entries": [
            {"kind": "reset_trace_seed", "identity": reset, "first_cycle": 0},
            {"kind": "reset_trace_seed", "identity": sa1, "first_cycle": 5},
        ],
        "cfg": {"schema_version": 1, "blocks": blocks},
    }
    vectors = {
        "schema_version": 1,
        "cartridge": {"mapping": "sa1-lorom", "payload_size": 32768, "copier_header": False},
        "processors": {
            "scpu": {"vectors": [
                {"name": "emulation_reset", "target_cpu_address": 0x8000},
                {"name": "native_nmi", "target_cpu_address": 0x8200},
            ]},
            "sa1": {"observed_reset_entry": {"target_cpu_address": 0x8100}},
        },
    }
    rom = bytearray(32768)
    rom[0:3] = bytes([0x4C, 0x00, 0x82]) if direct_jump else bytes([0xEA, 0xEA, 0xEA])
    rom[0x100] = 0xEA
    rom[0x200] = 0xEA
    return vectors, cfg, bytes(rom)


def test_native_nmi_edge_is_provenance_not_an_instruction_successor() -> None:
    vectors, cfg, rom = inputs()
    result = lift_reset_paths(
        vectors, cfg, rom,
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
    )
    source = cfg["entries"][0]["identity"]
    normal = cfg["cfg"]["blocks"][1]["identity"]
    nmi = cfg["cfg"]["blocks"][2]["identity"]
    source_block = next(block for block in result["blocks"] if block["identity"] == source)
    assert source_block["status"] == "decoded"
    assert [edge["target"] for edge in result["edges"] if edge["source"] == source] == [normal]
    assert result["async_edges"] == [{
        "source": source, "target": nmi,
        "kind": "asynchronous_interrupt", "vector": "native_nmi",
    }]
    schema = json.loads((ROOT / "schemas/recompiler/lifted-reset-cfg.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(result)


def test_direct_jump_to_native_nmi_address_stays_a_normal_edge() -> None:
    vectors, cfg, rom = inputs(direct_jump=True)
    result = lift_reset_paths(
        vectors, cfg, rom,
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
    )
    source = cfg["entries"][0]["identity"]
    nmi = cfg["cfg"]["blocks"][2]["identity"]
    assert result["async_edges"] == []
    assert any(edge["source"] == source and edge["target"] == nmi for edge in result["edges"])


def test_dynamic_return_to_native_nmi_address_is_not_reclassified() -> None:
    vectors, cfg, rom = inputs(direct_jump=True)
    mutable_rom = bytearray(rom)
    mutable_rom[0] = 0x60  # RTS; observed target comes from runtime stack state.
    result = lift_reset_paths(
        vectors, cfg, bytes(mutable_rom),
        allow_observed_dynamic_control_flow=True,
        analyze_all_observed_identities=True,
    )
    source = cfg["entries"][0]["identity"]
    nmi = cfg["cfg"]["blocks"][2]["identity"]
    assert result["async_edges"] == []
    assert any(edge["source"] == source and edge["target"] == nmi for edge in result["edges"])
