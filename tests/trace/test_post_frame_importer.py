from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from kss_trace.importer import TraceFormatError
from kss_trace.post_frame_importer import import_post_frame_lines, main


SYNTHETIC = [
    "diagnostic KSS_POST_FRAME_START_V1|8|600",
    "KSS_POST_FRAME_ANCHOR_V1|scpu|100|008000|1|1|1",
    "KSS_POST_FRAME_ANCHOR_V1|sa1|90|008100|1|1|1",
    "KSS_TRACE_V1|1|scpu|101|008001|1|1|1",
    "KSS_TRACE_V1|2|sa1|92|008101|1|1|1",
    "KSS_TRACE_V1|3|scpu|104|008002|1|1|1",
    "KSS_POST_FRAME_END_V1|3|visible|4|256|239|61184|17",
]


def test_sanitizes_anchored_visible_continuation_and_validates_schema() -> None:
    result = import_post_frame_lines(SYNTHETIC)
    segment = result["capture"]["route_segment"]
    assert segment["contract"] == "first-end-frame-to-first-visible-v1"
    assert segment["frames_elapsed"] == 4
    assert segment["visible_frame"]["nonblack_pixels"] == 17
    assert [item["processor"] for item in segment["continuation_from"]] == ["scpu", "sa1"]
    root = Path(__file__).resolve().parents[2]
    schema = json.loads((root / "schemas/trace/coverage.schema.json").read_text())
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(result)


@pytest.mark.parametrize(
    ("lines", "message"),
    [
        (SYNTHETIC[:-1], "missing visible-frame"),
        ([*SYNTHETIC[:-1], "KSS_POST_FRAME_ABORT_V1|frame_limit|3|600"], "aborted"),
        ([line for line in SYNTHETIC if "ANCHOR_V1|sa1" not in line], "both continuation anchors"),
        ([*SYNTHETIC[:-1], "KSS_POST_FRAME_END_V1|3|visible|4|256|239|61184|0"], "does not prove"),
    ],
)
def test_rejects_partial_or_unproven_capture(lines, message) -> None:
    with pytest.raises(TraceFormatError, match=message):
        import_post_frame_lines(lines)


def test_cli_writes_rom_free_identity_metadata(tmp_path: Path) -> None:
    raw = tmp_path / "private.log"
    output = tmp_path / "route.json"
    raw.write_text("\n".join(SYNTHETIC), encoding="utf-8")
    assert main([str(raw), str(output)]) == 0
    rendered = output.read_text(encoding="utf-8")
    assert json.loads(rendered) == import_post_frame_lines(SYNTHETIC)
    for forbidden in ("opcode", "bytes_hex", "register", "memory", "rom_path"):
        assert forbidden not in rendered.lower()
