from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def test_sa1_first_endframe_domain_is_schema_valid_and_source_anchored() -> None:
    artifact = load("analysis/differential/sa1-first-endframe-domain.json")
    schema = load("schemas/differential/sa1-first-endframe-domain.schema.json")
    jsonschema.validate(artifact, schema)

    parity = load("analysis/differential/first-frame-parity-audit.json")
    reference = parity["hardware_reference"]
    observation = parity["native_runtime_observation"]

    assert artifact["reference"] == {
        "target_master_clock": reference["master_clock"],
        "target_sa1_cycle": reference["processors"]["sa1"]["cycle"],
    }
    assert artifact["runtime_start"] == {
        "ready_master_clock": observation["local_timing"]["sa1_ready_master"],
        "sa1_cycle": observation["local_timing"]["sa1_ready_master"] // 2,
        "identity": {
            "processor": "sa1",
            "pc": observation["frontiers"]["sa1_pc"],
            "mode": "e0m0x0",
        },
    }


def test_sa1_first_endframe_domain_arithmetic_proves_the_hard_boundary() -> None:
    artifact = load("analysis/differential/sa1-first-endframe-domain.json")
    start = artifact["runtime_start"]
    advance = artifact["whole_instruction_advance"]
    target = artifact["reference"]
    residual = artifact["residual"]

    assert advance["completed_master_clocks"] == advance["completed_sa1_cycles"] * 2
    assert advance["ready_master_clock"] == (
        start["ready_master_clock"] + advance["completed_master_clocks"]
    )
    assert advance["sa1_cycle"] == start["sa1_cycle"] + advance["completed_sa1_cycles"]
    assert residual["master_clocks"] == (
        target["target_master_clock"] - advance["ready_master_clock"]
    )
    assert residual["sa1_cycles"] * 2 == residual["master_clocks"]
    assert target["target_sa1_cycle"] - advance["sa1_cycle"] == residual["sa1_cycles"]
    assert 0 < residual["sa1_cycles"] < residual["next_whole_instruction_sa1_cycles"]
    assert advance["status"] == "target_inside_instruction"
    assert artifact["claims"] == {
        "exact_boundary_reached": False,
        "partial_instruction_fabricated": False,
        "state_patched": False,
        "hard_boundary": "whole_instruction_timing",
    }


def test_sa1_domain_artifact_matches_the_verified_runtime_contract() -> None:
    artifact = load("analysis/differential/sa1-first-endframe-domain.json")
    header = (ROOT / "include/kss/sa1_frame_domain.hpp").read_text(encoding="utf-8")
    implementation = (ROOT / "src/runtime/sa1_frame_domain.cpp").read_text(encoding="utf-8")
    runtime_test = (ROOT / "tests/runtime/sa1_frame_domain_tests.cpp").read_text(
        encoding="utf-8"
    )

    assert "target_inside_instruction" in header
    assert "instruction_master > remaining" in implementation
    assert "0x008c58U" in implementation
    for literal in ("152352U", "306900U", "19318U", "77272U", "306896U", "153448U"):
        assert literal in runtime_test
    assert artifact["whole_instruction_advance"] == {
        "status": "target_inside_instruction",
        "completed_blocks": 19318,
        "completed_sa1_cycles": 77272,
        "completed_master_clocks": 154544,
        "ready_master_clock": 306896,
        "sa1_cycle": 153448,
        "identity": {"processor": "sa1", "pc": 0x008C58, "mode": "e0m0x0"},
    }
    assert artifact["residual"] == {
        "master_clocks": 4,
        "sa1_cycles": 2,
        "next_whole_instruction_sa1_cycles": 5,
        "reason": "the target lies inside the next modeled poll-load instruction",
    }


def test_sa1_domain_artifact_contains_no_rom_or_private_state_payload() -> None:
    path = ROOT / "analysis/differential/sa1-first-endframe-domain.json"
    rendered = path.read_text(encoding="utf-8")
    assert "sanitized-identities-counts-and-timing-only" in rendered
    for forbidden in (
        '"opcode"',
        '"bytes_hex"',
        '"architectural_state_sha256"',
        '"registers"',
        '"memory"',
        '"trace_records"',
    ):
        assert forbidden not in rendered
