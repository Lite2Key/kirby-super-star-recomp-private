from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from differential.kss_diff.frame_oracle import (
    FrameOracleError,
    parse_frame_oracle,
    summarize_frame_oracle,
)


ROOT = Path(__file__).resolve().parents[2]


def raw_log() -> str:
    return "\n".join([
        "Mesen non-oracle banner",
        "KSS_FRAME_START_V1|1000000",
        "KSS_FRAME_WRITE_V1|1|scpu|2|002100|143",
        "KSS_FRAME_WRITE_V1|2|scpu|4|00420B|1",
        "KSS_FRAME_WRITE_V1|3|sa1|5|806000|85",
        "KSS_FRAME_STATE_V1|scpu|7|008123|1|2|3|4|511|5|52|1",
        "KSS_FRAME_STATE_V1|sa1|9|008C23|6|7|8|9|510|10|5|0",
        "KSS_FRAME_END_V1|1|100|7|9|3",
        "",
    ])


def test_summary_is_value_free_deterministic_and_schema_valid(tmp_path: Path) -> None:
    path = tmp_path / "frame.log"
    path.write_text(raw_log(), encoding="utf-8")
    result = summarize_frame_oracle(path)
    assert result["termination"] == "first_end_frame"
    assert result["clocks"] == {"master": 100, "processors": {"scpu": 7, "sa1": 9}}
    assert result["writes"]["total_records"] == 3
    assert [item["records"] for item in result["writes"]["processors"]] == [2, 1]
    assert [item["records"] for item in result["register_events"]] == [1, 1]
    assert summarize_frame_oracle(path) == result

    rendered = json.dumps(result, sort_keys=True)
    for private_value in ("008123", "008C23", "002100", "00420B", "806000", "143", "85"):
        assert private_value not in rendered
    schema = json.loads((ROOT / "schemas/differential/first-frame-summary.schema.json").read_text())
    jsonschema.validate(result, schema)


@pytest.mark.parametrize(
    ("mutation", "message"),
    [
        (lambda text: text.replace("|2|scpu|4|", "|3|scpu|4|"), "ordinals"),
        (lambda text: text.replace("|1|100|7|9|3", "|1|100|7|9|2"), "write count"),
        (lambda text: text.replace("STATE_V1|sa1|9", "STATE_V1|sa1|10"), "cycle disagrees"),
        (lambda text: text.replace("KSS_FRAME_END_V1", "KSS_FRAME_ABORT_V1"), "aborted"),
        (lambda text: text.replace("KSS_FRAME_STATE_V1|sa1", "KSS_FRAME_UNKNOWN_V1|sa1"), "unknown"),
    ],
)
def test_parser_rejects_corrupt_or_incomplete_oracles(
    tmp_path: Path, mutation, message: str,
) -> None:
    path = tmp_path / "bad.log"
    path.write_text(mutation(raw_log()), encoding="utf-8")
    with pytest.raises(FrameOracleError, match=message):
        parse_frame_oracle(path)


def test_harness_is_first_end_frame_bounded_and_private() -> None:
    script = (ROOT / "tools/mesen/first_frame_oracle.lua").read_text(encoding="utf-8")
    for required in (
        "emu.eventType.endFrame", "emu.getMasterClock", "emu.getCpuCycleCount",
        "emu.getCpuState", "emu.callbackType.write", "MAX_WRITES", "emu.stop(0)",
        "KSS_FRAME_STATE_V1", "KSS_FRAME_WRITE_V1", "KSS_FRAME_END_V1",
    ):
        assert required in script
    assert "KSS_FRAME_WRITE_V1" in script and "value" in script
    assert ".private" in script


def test_committed_first_frame_reference_is_strict_and_value_free() -> None:
    result = json.loads(
        (ROOT / "analysis/differential/first-frame-reference.json").read_text(encoding="utf-8")
    )
    schema = json.loads((ROOT / "schemas/differential/first-frame-summary.schema.json").read_text())
    jsonschema.validate(result, schema)
    assert result["termination"] == "first_end_frame"
    assert result["writes"]["total_records"] > 0
    assert all(item["records"] > 0 for item in result["register_events"])

    def keys(value):
        if isinstance(value, dict):
            yield from value.keys()
            for nested in value.values():
                yield from keys(nested)
        elif isinstance(value, list):
            for nested in value:
                yield from keys(nested)

    forbidden = {"pc", "a", "x", "y", "d", "sp", "dbr", "status", "value", "address"}
    assert forbidden.isdisjoint(keys(result))
