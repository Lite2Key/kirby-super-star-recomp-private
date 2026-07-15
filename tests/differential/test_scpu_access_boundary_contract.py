import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_scpu_exact_boundary_artifact_exposes_phase_difference() -> None:
    artifact = json.loads(
        (ROOT / "analysis/differential/scpu-first-endframe-boundary.json")
        .read_text(encoding="utf-8")
    )
    window = artifact["runtime_access_window"]
    target = artifact["target_master_clock"]

    assert artifact["boundary"] == "first_end_frame"
    assert window["block_start_master_clock"] < target < window["block_end_master_clock"]
    assert window["block_end_master_clock"] - target == window["remaining_master_clocks"]
    assert window["current_access_start_master_clock"] <= target
    assert (
        target - window["current_access_start_master_clock"]
        == window["elapsed_in_current_access_master_clocks"]
    )
    assert window["committed_cpu_context_pc_hex"] == "00:D65D"
    assert window["sequencer_pc_hex"] == "00:D65E"
    assert window["current_access_index"] == 0
    assert window["accesses"] == 5
    assert window["current_access_start_master_clock"] == 306898
    assert window["elapsed_in_current_access_master_clocks"] == 2
    assert artifact["reference"]["sequencer_pc_hex"] == "00:D659"
    assert artifact["claims"] == {
        "exact_runtime_suspension_represented": True,
        "whole_block_rounding_required": False,
        "pc_or_full_state_parity_proven": False,
        "first_hard_divergence": artifact["claims"]["first_hard_divergence"],
    }


def test_scpu_boundary_helper_is_value_free_and_non_mutating() -> None:
    header = (ROOT / "include/kss/scpu_access_boundary.hpp").read_text(encoding="utf-8")
    implementation = (ROOT / "src/runtime/scpu_access_boundary.cpp").read_text(
        encoding="utf-8"
    )
    assert "CpuContext&" not in header
    assert "Bus&" not in header
    assert "read8(" not in implementation
    assert "write8(" not in implementation
    assert "target == cursor" in implementation
    assert "result.total_accesses = accesses.size()" in implementation
    boot = (ROOT / "src/runtime/boot_probe.cpp").read_text(encoding="utf-8")
    assert "result.scpu_first_frame_boundary = observe_scpu_access_boundary" in boot
