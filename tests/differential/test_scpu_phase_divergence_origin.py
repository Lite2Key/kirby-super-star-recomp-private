import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load(path: str) -> dict:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def test_origin_is_anchored_to_committed_timing_artifacts() -> None:
    origin = load("analysis/differential/scpu-phase-divergence-origin.json")
    reset = load("analysis/differential/reset-block-reference.json")["blocks"][1]
    first_frame = load("analysis/coverage/first-frame-dual.json")

    matched = origin["last_matched_sa1_slice"]
    assert matched["reference_cycle_start"] == reset["cycle_start"] == 1454
    assert matched["reference_cycle_end"] == reset["cycle_end"] == 22011
    assert matched["reference_cycle_delta"] == reset["cycle_delta"] == 20557
    assert matched["runtime_cycle_delta"] == 20557

    checkpoint = origin["accumulated_checkpoint_divergence"]
    # This artifact deliberately preserves the historical pre-interleaving
    # divergence origin; the live scheduler now records a later SA-1 frame
    # observation in sa1-first-endframe-domain.json.
    assert checkpoint["runtime_sa1_cycle"] == 76176
    assert checkpoint["runtime_ready_master_clock"] == 152352
    sa1_poll = next(
        block for block in first_frame["blocks"]
        if block["processor"] == "sa1" and block["pc"] == 0x8C58
    )
    assert checkpoint["reference_first_identity_cycle"] == sa1_poll["first_cycle"] == 112832
    assert checkpoint["sa1_cycle_shortfall"] == 112832 - 76176
    assert checkpoint["master_clock_shortfall"] == 2 * checkpoint["sa1_cycle_shortfall"]


def test_first_post_reset_instruction_and_final_loop_are_not_conflated() -> None:
    origin = load("analysis/differential/scpu-phase-divergence-origin.json")
    first_frame = load("analysis/coverage/first-frame-dual.json")
    blocks = {(item["processor"], item["pc"]): item for item in first_frame["blocks"]}

    post_reset = origin["earliest_provable_divergences"][1]
    assert post_reset["instruction_pc_hex"] == "00:8C23"
    assert post_reset["reference_instruction_cycles"] == (
        blocks[("sa1", 0x8C26)]["first_cycle"] - blocks[("sa1", 0x8C23)]["first_cycle"]
    ) == 5
    assert post_reset["runtime_modeled_instruction_cycles"] == 3

    final = origin["final_upload_loop_audit"]
    assert final["reference_d655_to_d658_cycles"] == (
        blocks[("scpu", 0xD658)]["last_cycle"] - blocks[("scpu", 0xD655)]["last_cycle"]
    ) == 4
    assert final["reference_d658_taken_to_d655_cycles"] == 3
    assert final["runtime_d655_access_master_clocks"] == 3 * 8 + 6
    assert final["runtime_d658_access_master_clocks"] == 2 * 8
    assert not final["local_access_sequence_or_duration_bug_proven"]
    assert not origin["claims"]["safe_local_correction_available"]


def test_runtime_sources_retain_the_exposed_causal_boundary() -> None:
    boot = (ROOT / "src/runtime/boot_probe.cpp").read_text(encoding="utf-8")
    timing_header = (ROOT / "include/kss/sa1_timing.hpp").read_text(encoding="utf-8")
    timing = (ROOT / "src/runtime/sa1_timing.cpp").read_text(encoding="utf-8")
    generated = (ROOT / "src/recompiled/first_frame_blocks.cpp").read_text(encoding="utf-8")
    assert "result.sa1.pc = 0x8bf4" in boot
    assert "align_domain(ClockDomain::sa1, result.sa1_release_master)" in boot
    assert "elapsed - release_cycles" in boot
    assert "reset_domain_switches" in boot
    assert "sa1_reset_release_origin_cycles" in timing_header
    assert "sa1_reset_release_origin_cycles" in generated
    assert "lookup_sa1_post_reset_timing" in generated
    assert "lookup_sa1_post_reset_mvn_completion_wait" in generated
    assert "0x008c23U" in timing
    assert "35175U" in timing
