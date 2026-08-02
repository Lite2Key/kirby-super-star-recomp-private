from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from differential.kss_diff.sa1_slice import Sa1SliceError, summarize_sa1_slice


ROOT = Path(__file__).resolve().parents[2]


def private_log() -> str:
    return "\n".join([
        "Mesen banner",
        "KSS_SA1_SLICE_START_V1|200000|240",
        "KSS_SA1_SLICE_STATE_V1|1|100|008C36|91|32768|1|2|0|511|0|5|0",
        "KSS_SA1_SLICE_STATE_V1|2|102|008C37|169|32768|1|2|32768|511|0|133|0",
        "KSS_SA1_SLICE_STATE_V1|3|110|008C5B|16|0|1|2|32768|511|0|7|0",
        "KSS_SA1_SLICE_POLL_V1|110|200|90",
        "KSS_SA1_SLICE_STATE_V1|4|200|008C5D|156|0|1|2|32768|511|0|7|0",
        "KSS_SA1_SLICE_WRITE_V1|1|4|204|003000|0",
        "KSS_SA1_SLICE_STATE_V1|5|205|008C60|92|0|1|2|32768|511|0|7|0",
        "KSS_SA1_SLICE_END_V1|next_unsupported|5|1|008C60|92",
        "",
    ])


def test_sanitizer_proves_tcd_contract_without_exporting_state_values(tmp_path: Path) -> None:
    path = tmp_path / "slice.log"
    path.write_text(private_log(), encoding="utf-8")
    result = summarize_sa1_slice(path)
    assert result["start_identity"] == {"pc": 0x8C36, "opcode": 0x5B, "mode": "e0m0x0"}
    assert result["stop_identity"]["pc"] == 0x8C60
    assert result["tcd_transition"]["observed_total_cycles"] == 2
    assert result["tcd_transition"]["writes"] == 0
    assert result["slice"]["write_regions"] == {"sa1_iram": 1}
    assert result["polling_boundary"]["cycle_delta"] == 90
    rendered = json.dumps(result)
    for forbidden in ('"a"', '"x"', '"y"', '"d"', '"sp"', '"dbr"', '"status"', '"address"', '"value"'):
        assert forbidden not in rendered


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ("|32768|511|0|133|0", "|1|511|0|133|0", "copy A into D"),
        ("|32768|511|0|133|0", "|32768|511|0|5|0", "status transition"),
        ("WRITE_V1|1|4|204", "WRITE_V1|1|1|101", "bus write"),
        ("END_V1|next_unsupported|5|1|008C60|92", "END_V1|next_unsupported|5|1|008C61|92", "end identity"),
        ("POLL_V1|110|200|90", "POLL_V1|110|200|91", "poll-cycle delta"),
    ],
)
def test_corrupt_private_slice_is_rejected(tmp_path: Path, old: str, new: str, message: str) -> None:
    path = tmp_path / "bad.log"
    path.write_text(private_log().replace(old, new), encoding="utf-8")
    with pytest.raises(Sa1SliceError, match=message):
        summarize_sa1_slice(path)


def test_committed_reference_validates_against_strict_schema() -> None:
    artifact = json.loads(
        (ROOT / "analysis/differential/sa1-tcd-slice-reference.json").read_text(encoding="utf-8")
    )
    schema = json.loads(
        (ROOT / "schemas/differential/sa1-tcd-slice.schema.json").read_text(encoding="utf-8")
    )
    jsonschema.validate(artifact, schema)
    assert artifact["slice"]["state_records"] == 16
    assert artifact["slice"]["write_records"] == 16
    assert artifact["tcd_transition"]["base_cycles"] == 2
