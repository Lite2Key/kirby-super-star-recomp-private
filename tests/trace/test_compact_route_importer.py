from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from kss_trace.compact_route_importer import import_compact_route_lines, main
from kss_trace.importer import TraceFormatError


SYNTHETIC = [
    "diagnostic KSS_COMPACT_ROUTE_START_V1|8|600|8|8",
    "KSS_COMPACT_ROUTE_BLOCK_V1|scpu|008000|1|1|1|2|0|4",
    "KSS_COMPACT_ROUTE_BLOCK_V1|scpu|008001|1|1|1|1|2|2",
    "KSS_COMPACT_ROUTE_BLOCK_V1|sa1|008100|1|1|1|2|0|1",
    "KSS_COMPACT_ROUTE_EDGE_V1|scpu|008000|1|1|1|scpu|008001|1|1|1|1",
    "KSS_COMPACT_ROUTE_EDGE_V1|scpu|008001|1|1|1|scpu|008000|1|1|1|1",
    "KSS_COMPACT_ROUTE_EDGE_V1|sa1|008100|1|1|1|sa1|008100|1|1|1|1",
    "KSS_COMPACT_ROUTE_END_V1|5|visible|4|2|2|4|3|3|3",
]


def test_imports_compact_reset_to_visible_coverage_and_validates_schema() -> None:
    result = import_compact_route_lines(SYNTHETIC)
    capture = result["capture"]
    route = capture["route_capture"]
    assert capture["event_count"] == 5
    assert route["contract"] == "reset-to-first-visible-v1"
    assert route["aggregation"] == "in_emulator"
    assert route["visible_frame"]["nonblack_pixels"] == 3
    assert result["processors"] == {
        "scpu": {"events": 3, "unique_blocks": 2},
        "sa1": {"events": 2, "unique_blocks": 1},
    }
    root = Path(__file__).resolve().parents[2]
    schema = json.loads((root / "schemas/trace/coverage.schema.json").read_text())
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(result)


def test_output_is_deterministic_and_contains_only_sanitized_identity_metadata() -> None:
    first = import_compact_route_lines(SYNTHETIC)
    reordered = [SYNTHETIC[0], *reversed(SYNTHETIC[1:-1]), SYNTHETIC[-1]]
    assert first == import_compact_route_lines(reordered)
    rendered = json.dumps(first, sort_keys=True).lower()
    for forbidden in ("opcode", "bytes_hex", "register", "memory", "rom_path", "framebuffer"):
        assert forbidden not in rendered


@pytest.mark.parametrize(
    ("lines", "message"),
    [
        (SYNTHETIC[:-1], "missing compact route visible"),
        ([*SYNTHETIC[:-1], "KSS_COMPACT_ROUTE_ABORT_V1|event_limit|8|3|3|3"], "aborted"),
        ([line for line in SYNTHETIC if "BLOCK_V1|sa1" not in line], "block count disagrees"),
        ([line.replace("END_V1|5", "END_V1|6") for line in SYNTHETIC], "block hit sum"),
        ([line.replace("EDGE_V1|sa1", "EDGE_V1|scpu", 1) for line in SYNTHETIC], "crosses processor"),
    ],
)
def test_rejects_partial_or_inconsistent_aggregates(lines, message: str) -> None:
    with pytest.raises(TraceFormatError, match=message):
        import_compact_route_lines(lines)


def test_cli_writes_rom_free_aggregate_json(tmp_path: Path) -> None:
    source = tmp_path / "private.log"
    output = tmp_path / "coverage.json"
    source.write_text("\n".join(SYNTHETIC), encoding="utf-8")
    assert main([str(source), str(output)]) == 0
    assert json.loads(output.read_text(encoding="utf-8")) == import_compact_route_lines(SYNTHETIC)
