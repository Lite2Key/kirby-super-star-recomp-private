from __future__ import annotations

import hashlib
import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def test_first_frame_parity_audit_is_schema_valid_and_source_anchored() -> None:
    audit = load("analysis/differential/first-frame-parity-audit.json")
    schema = load("schemas/differential/first-frame-parity-audit.schema.json")
    jsonschema.validate(audit, schema)

    frame = load("analysis/differential/first-frame-reference.json")
    ppu = load("analysis/differential/ppu-first-frame-reference.json")
    spc = load("analysis/differential/spc-first-frame-reference.json")
    hardware = audit["hardware_reference"]

    assert hardware["boundary"] == frame["termination"] == ppu["boundary"]
    assert hardware["master_clock"] == frame["clocks"]["master"] == 306900
    assert hardware["cpu_writes"]["records"] == frame["writes"]["total_records"]
    assert hardware["cpu_writes"]["chain_sha256"] == frame["writes"]["chain_sha256"]
    assert hardware["register_events"]["ppu_records"] == frame["register_events"][0]["records"]
    assert hardware["register_events"]["dma_records"] == frame["register_events"][1]["records"]
    assert hardware["spc"]["execution_records"] == spc["execution"]["records"]
    assert hardware["spc"]["port_records"] == spc["ports"]["records"]
    assert hardware["frame"] == {
        "width": ppu["width"],
        "height": ppu["height"],
        "forced_blank": ppu["forced_blank"],
        "brightness": ppu["brightness"],
        "nonblack_pixels": ppu["nonblack_pixels"],
        "rgba_sha256": ppu["rgba_sha256"],
    }


def test_native_observation_claim_stops_at_pixel_parity() -> None:
    audit = load("analysis/differential/first-frame-parity-audit.json")
    identity = load("analysis/coverage/boot-probe-identity-coverage.json")
    native = audit["native_runtime_observation"]

    assert native["generated_identities"] == {
        "inventory": identity["inventory_count"],
        "executed": identity["executed_count"],
        "missing": identity["missing_count"],
    }
    live = identity["runtime_ipl_observation"]
    assert native["causal_boot"]["spc_start_acknowledged"] == live["start_acknowledged"]
    assert native["causal_boot"]["patched_state"] == live["patched_state"]
    assert native["local_timing"]["spc_instructions"] == live["spc_instructions"]
    assert native["local_timing"]["spc_architectural_cycles"] == live["spc_architectural_cycles"]
    assert native["frame"]["rgba_sha256"] == audit["hardware_reference"]["frame"]["rgba_sha256"]
    assert native["frame"]["nonblack_pixels"] == 0
    black_rgba = bytes((0, 0, 0, 255)) * (256 * 239)
    assert hashlib.sha256(black_rgba).hexdigest() == native["frame"]["rgba_sha256"]

    assert not audit["verdict"]["full_first_frame_parity_proven"]
    assert set(audit["claims"]["not_proven"]) == {
        "same_hardware_boundary",
        "scpu_final_state",
        "sa1_final_state",
        "cpu_write_chain",
        "ppu_register_chain",
        "dma_register_chain",
        "spc_execution_chain",
        "spc_port_chain",
        "cross_processor_event_order",
        "interrupt_delivery_at_hardware_time",
    }


def test_first_frame_parity_audit_preserves_private_data_boundary() -> None:
    audit_path = ROOT / "analysis/differential/first-frame-parity-audit.json"
    rendered = audit_path.read_text(encoding="utf-8")
    audit = json.loads(rendered)

    assert set(audit["data_boundary"].values()) == {
        "sanitized-digests-counts-and-identities-only", False
    }
    for forbidden in (
        "bytes_hex", "rom_offset", "opcode", "mnemonic", "operand",
        '"pixels":', '"trace_records":',
    ):
        assert forbidden not in rendered
    for digest in (
        audit["hardware_reference"]["cpu_writes"]["chain_sha256"],
        audit["hardware_reference"]["spc"]["state_chain_sha256"],
        audit["native_runtime_observation"]["frame"]["bmp_sha256"],
    ):
        assert len(digest) == 64
        int(digest, 16)
