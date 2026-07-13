from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from differential.kss_diff.sa1_tail import (
    EXPECTED_STATE_PCS,
    EXPECTED_TAIL_PCS,
    Sa1TailError,
    summarize_sa1_tail,
)


ROOT = Path(__file__).resolve().parents[2]


def private_trace() -> str:
    lines = [
        "Mesen banner",
        "KSS_TRACE_START_V1|1000",
        "KSS_TRACE_V1|1|scpu|0|008004|1|1|1",
        "KSS_TRACE_V1|2|sa1|22011|008C23|0|0|0",
    ]
    for sequence, pc in enumerate(EXPECTED_TAIL_PCS, 3):
        lines.append(f"KSS_TRACE_V1|{sequence}|sa1|{22010 + sequence}|{pc:06X}|0|0|0")
    lines.extend([
        "KSS_TRACE_FIRST_FRAME_V1|22|1|21",
        "KSS_TRACE_END_V1|22|complete",
        "",
    ])
    return "\n".join(lines)


def private_slice() -> str:
    lines = ["KSS_SA1_SLICE_START_V1|200000|240"]
    pcs = (*EXPECTED_STATE_PCS, 0x008C5D, 0x008C60)
    for ordinal, pc in enumerate(pcs, 1):
        lines.append(
            f"KSS_SA1_SLICE_STATE_V1|{ordinal}|{100 + ordinal}|{pc:06X}|0|0|0|0|0|511|0|0|0"
        )
        if pc == 0x008C5B:
            lines.append("KSS_SA1_SLICE_POLL_V1|114|115|1")
    lines.extend([
        "KSS_SA1_SLICE_END_V1|next_unsupported|16|0|008C60|0",
        "",
    ])
    return "\n".join(lines)


def write_inputs(tmp_path: Path, trace: str | None = None, slice_log: str | None = None) -> tuple[Path, Path]:
    trace_path = tmp_path / "trace.log"
    slice_path = tmp_path / "slice.log"
    trace_path.write_text(trace if trace is not None else private_trace(), encoding="utf-8")
    slice_path.write_text(slice_log if slice_log is not None else private_slice(), encoding="utf-8")
    return trace_path, slice_path


def identity_set(items: list[dict]) -> set[tuple]:
    return {
        (
            item["processor"], item["pc"], item["mode"]["emulation"],
            item["mode"]["m8"], item["mode"]["x8"],
        )
        for item in items
    }


def test_sanitizer_separates_identity_order_from_architectural_proof(tmp_path: Path) -> None:
    trace, slice_path = write_inputs(tmp_path)
    result = summarize_sa1_tail(trace, slice_path)

    assert result["identity_route"]["observed_identity_count"] == 20
    assert result["identity_route"]["event_records"] == 20
    assert result["architectural_slice"]["state_verified_count"] == 14
    assert result["identity_order_only_remaining"]["identity_order_only_count"] == 6
    assert [item["pc"] for item in result["identity_route"]["identities"]] == list(EXPECTED_TAIL_PCS)
    assert [item["pc"] for item in result["architectural_slice"]["identities"]] == list(EXPECTED_STATE_PCS)
    assert [item["pc"] for item in result["identity_order_only_remaining"]["identities"]] == list(EXPECTED_TAIL_PCS[:6])

    rendered = json.dumps(result)
    for forbidden in (
        '"opcode"', '"a"', '"x"', '"y"', '"d"', '"sp"', '"dbr"',
        '"status"', '"address"', '"value"', '"bytes_hex"', '"rom_offset"',
    ):
        assert forbidden not in rendered


@pytest.mark.parametrize(
    ("trace_edit", "slice_edit", "message"),
    [
        (("|008C2F|0|0|0", "|008C2C|0|0|0"), None, "incomplete or reordered"),
        (("|008C26|0|0|0", "|008C26|1|1|1"), None, "left native 16-bit mode"),
        (("V1|3|sa1", "V1|4|sa1"), None, "ordinals are not contiguous"),
        (None, ("|008C3A|0|", "|008C37|0|"), "does not cover"),
    ],
)
def test_sanitizer_rejects_incomplete_or_ambiguous_private_evidence(
    tmp_path: Path,
    trace_edit: tuple[str, str] | None,
    slice_edit: tuple[str, str] | None,
    message: str,
) -> None:
    trace = private_trace()
    slice_log = private_slice()
    if trace_edit:
        trace = trace.replace(*trace_edit, 1)
    if slice_edit:
        slice_log = slice_log.replace(*slice_edit, 1)
    trace_path, slice_path = write_inputs(tmp_path, trace, slice_log)
    with pytest.raises(Sa1TailError, match=message):
        summarize_sa1_tail(trace_path, slice_path)


def test_committed_sa1_tail_reference_is_strict_value_free_and_inventory_exact() -> None:
    artifact = json.loads(
        (ROOT / "analysis/differential/sa1-post-reset-reference.json").read_text(encoding="utf-8")
    )
    schema = json.loads(
        (ROOT / "schemas/differential/sa1-post-reset-reference.schema.json").read_text(encoding="utf-8")
    )
    inventory = json.loads(
        (ROOT / "analysis/coverage/first-frame-dual.json").read_text(encoding="utf-8")
    )
    reset = json.loads(
        (ROOT / "analysis/differential/reset-block-reference.json").read_text(encoding="utf-8")
    )
    jsonschema.validate(artifact, schema)

    all_sa1 = identity_set([item for item in inventory["blocks"] if item["processor"] == "sa1"])
    reset_end = next(item["cycle_end"] for item in reset["blocks"] if item["processor"] == "sa1")
    reset_sa1 = identity_set([
        item for item in inventory["blocks"]
        if item["processor"] == "sa1" and item["first_cycle"] <= reset_end
    ])
    tail = identity_set(artifact["identity_route"]["identities"])
    state_verified = identity_set(artifact["architectural_slice"]["identities"])
    identity_only = identity_set(artifact["identity_order_only_remaining"]["identities"])

    assert len(all_sa1) == 40
    assert len(reset_sa1) == 20
    assert tail == all_sa1 - reset_sa1
    assert len(tail) == artifact["identity_route"]["observed_identity_count"] == 20
    assert len(state_verified) == artifact["architectural_slice"]["state_verified_count"] == 14
    assert len(identity_only) == artifact["identity_order_only_remaining"]["identity_order_only_count"] == 6
    assert state_verified | identity_only == tail
    assert not state_verified & identity_only
