from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def test_live_first_frame_scheduler_is_schema_valid_and_source_anchored() -> None:
    report = load("analysis/differential/live-first-frame-scheduler.json")
    schema = load("schemas/differential/live-first-frame-scheduler.schema.json")
    frame = load("analysis/differential/first-frame-reference.json")
    identities = load("analysis/coverage/boot-probe-identity-coverage.json")
    jsonschema.validate(report, schema)

    target = report["reference"]["target_master_clock"]
    runtime = report["runtime"]
    assert report["boundary"] == frame["termination"]
    assert target == frame["clocks"]["master"] == runtime["master_now"]
    assert runtime["executed_generated_identities"] == identities["executed_count"]
    assert runtime["scpu"]["overshoot_master_clocks"] == (
        runtime["scpu"]["ready_master_clock"] - target
    )
    assert runtime["spc"]["overshoot_master_clocks"] == (
        runtime["spc"]["ready_master_clock"] - target
    )
    assert runtime["scpu"]["identity_pc"] != report["reference"]["scpu_pc_at_boundary"]
    assert runtime["sa1"]["ready_master_clock"] == 225694
    assert not runtime["sa1"]["interleaved_through_frame"]
    assert runtime["scpu"]["exact_boundary"]["access_start_master_clock"] == target
    assert runtime["scpu"]["exact_boundary"]["sequencer_pc"] != (
        report["reference"]["scpu_pc_at_boundary"]
    )
    spc_exact = runtime["spc"]["exact_boundary"]
    assert spc_exact["architectural_entry_master_clock"] < target
    assert spc_exact["completion_master_clock"] == runtime["spc"]["ready_master_clock"]
    assert spc_exact["elapsed_master_clocks"] == (
        target - spc_exact["architectural_entry_master_clock"]
    )


def test_live_first_frame_scheduler_preserves_private_data_boundary() -> None:
    path = ROOT / "analysis/differential/live-first-frame-scheduler.json"
    rendered = path.read_text(encoding="utf-8")
    report = json.loads(rendered)

    assert report["data_boundary"] == {
        "policy": "sanitized-identities-counts-and-timing-only",
        "rom_bytes_included": False,
        "spc_ipl_bytes_included": False,
        "private_trace_records_included": False,
        "patched_state": False,
    }
    assert not report["claims"]["full_first_frame_parity_proven"]
    for forbidden in (
        "bytes_hex", "rom_offset", "opcode", "mnemonic", "operand",
        '"trace_records":', '"ipl_bytes":',
    ):
        assert forbidden not in rendered
