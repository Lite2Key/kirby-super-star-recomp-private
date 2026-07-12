from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from tools.differential.kss_diff.compare import compare_logs, parse_log


ROOT = Path(__file__).resolve().parents[2]


def log(*, a: int = 1, write: int = 7, end_total: int = 2) -> str:
    return "\n".join((
        "KSS_DIFF_START_V1|1",
        f"KSS_DIFF_STATE_V1|1|scpu|0|008004|{a}|2|3|4|511|0|52|1",
        "KSS_DIFF_STATE_V1|2|sa1|10|008BF4|5|6|7|8|511|0|52|1",
        f"KSS_DIFF_WRITE_V1|scpu|000100|{write}",
        f"KSS_DIFF_END_V1|{end_total}|limit|1|1",
        "",
    ))


def write(path: Path, text: str) -> Path:
    path.write_text(text, encoding="utf-8")
    return path


def test_equal_private_logs_produce_value_free_passing_summary(tmp_path: Path) -> None:
    reference = write(tmp_path / "reference.log", log())
    candidate = write(tmp_path / "candidate.log", log())
    report = compare_logs(reference, candidate)
    assert report["passed"] is True
    assert report["mismatch_count"] == 0
    assert report["processors"] == {"scpu": 1, "sa1": 1}
    serialized = json.dumps(report)
    assert "008004" not in serialized
    assert '"a"' not in serialized
    schema = json.loads((ROOT / "schemas" / "differential" / "summary.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(report, schema)


def test_register_and_write_mismatches_report_fields_not_values(tmp_path: Path) -> None:
    reference = write(tmp_path / "reference.log", log())
    candidate = write(tmp_path / "candidate.log", log(a=9, write=8))
    report = compare_logs(reference, candidate)
    assert report["passed"] is False
    assert report["mismatch_count"] == 2
    assert report["mismatches"] == [
        {"record": "state", "index": 0, "fields": ["a"]},
        {"record": "write", "index": 0, "fields": ["value"]},
    ]


@pytest.mark.parametrize("text, message", [
    ("KSS_DIFF_START_V1|1\n", "start and end"),
    (log(end_total=1), "end marker"),
    (log().replace("|2|sa1|", "|3|sa1|"), "sequence"),
    (log().replace("008004", "1000000"), "PC"),
    (log().replace("|limit|1|1", "|limit|2|0"), "processor counts"),
])
def test_malformed_or_unbounded_logs_are_rejected(tmp_path: Path, text: str, message: str) -> None:
    with pytest.raises(ValueError, match=message):
        parse_log(write(tmp_path / "bad.log", text))


def test_mesen_harness_is_bounded_and_uses_private_state_apis() -> None:
    script = (ROOT / "tools" / "mesen" / "differential_trace.lua").read_text(encoding="utf-8")
    for required in (
        "local EVENTS_PER_CPU = 256", "emu.getCpuState", "emu.getState",
        "emu.getCpuCycleCount", "emu.callbackType.exec", "emu.callbackType.write",
        "cart.coprocessor.cpu.", "KSS_DIFF_STATE_V1", "KSS_DIFF_WRITE_V1", "emu.stop(0)",
    ):
        assert required in script
