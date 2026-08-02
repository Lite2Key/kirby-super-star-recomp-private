from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def test_spc_suspension_artifact_is_schema_valid_and_arithmetically_exact() -> None:
    artifact = load("analysis/differential/spc-first-endframe-suspension.json")
    schema = load(
        "schemas/differential/spc-first-endframe-suspension.schema.json"
    )
    jsonschema.validate(artifact, schema)

    clock = artifact["clock_contract"]
    entry = artifact["architectural_entry"]
    pending = artifact["pending_instruction"]
    phase = artifact["in_flight_observation"]

    entry_scaled = entry["spc_cycles"] * clock["master_hz"]
    assert entry["ready_master_clock"] == entry_scaled // clock["spc_hz"]
    assert entry["rational_remainder"] == entry_scaled % clock["spc_hz"]

    instruction_scaled = (
        pending["architectural_cycles"] * clock["master_hz"]
        + entry["rational_remainder"]
    )
    assert pending["completion_master_clock"] == (
        entry["ready_master_clock"] + instruction_scaled // clock["spc_hz"]
    )
    assert pending["completion_rational_remainder"] == (
        instruction_scaled % clock["spc_hz"]
    )
    assert phase["elapsed_master_clocks"] == (
        clock["target_master_clock"] - entry["ready_master_clock"]
    )
    assert phase["remaining_master_clocks"] == (
        pending["completion_master_clock"] - clock["target_master_clock"]
    )
    assert phase["observed_master_clock"] == clock["target_master_clock"]
    assert (
        entry["ready_master_clock"]
        < phase["observed_master_clock"]
        < pending["completion_master_clock"]
    )


def test_spc_suspension_claims_are_anchored_to_cpp_contract_and_tests() -> None:
    artifact = load("analysis/differential/spc-first-endframe-suspension.json")
    header = (ROOT / "include/kss/spc_exact_master.hpp").read_text(encoding="utf-8")
    implementation = (ROOT / "src/runtime/spc_exact_master.cpp").read_text(
        encoding="utf-8"
    )
    runtime_test = (ROOT / "tests/runtime/spc_exact_master_tests.cpp").read_text(
        encoding="utf-8"
    )

    assert "target_inside_instruction" in header
    assert "architectural_state_is_entry" in header
    assert "auto preview_core = core" in implementation
    assert "preview_core.set_port_write_sink(nullptr, nullptr)" in implementation
    for literal in (
        "306'890U",
        "306'900U",
        "306'973U",
        "83'904U",
        "1'000'992U",
        "phase.elapsed_master_clocks == 10U",
        "phase.remaining_master_clocks == 73U",
        "writes == 0U",
    ):
        assert literal in runtime_test
    assert artifact["claims"] == {
        "exact_master_observation_represented": True,
        "real_core_mutated_by_preview": False,
        "coordinator_advanced_by_preview": False,
        "speculative_port_events_emitted": False,
        "partial_architectural_state_fabricated": False,
        "state_patched": False,
        "state_rewound": False,
        "private_rom_runtime_capture_claimed": False,
    }


def test_spc_suspension_artifact_is_rom_free_and_sanitized() -> None:
    path = ROOT / "analysis/differential/spc-first-endframe-suspension.json"
    rendered = path.read_text(encoding="utf-8")
    artifact = json.loads(rendered)
    assert artifact["data_boundary"] == {
        "policy": "sanitized-timing-and-contract-only",
        "rom_bytes_included": False,
        "spc_ipl_bytes_included": False,
        "private_trace_records_included": False,
        "architectural_state_values_included": False,
    }
    assert artifact["evidence_kind"] == "rom_free_deterministic_contract_fixture"
    for forbidden in (
        '"opcode"',
        '"pc"',
        '"registers"',
        '"memory"',
        '"bytes_hex"',
        '"trace_records"',
        '"rom_offset"',
        '"ipl_bytes"',
    ):
        assert forbidden not in rendered
