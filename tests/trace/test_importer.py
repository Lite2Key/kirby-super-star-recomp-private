from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from kss_trace.importer import TraceFormatError, import_lines, main


SYNTHETIC_TRACE = [
    "Mesen diagnostic prefix KSS_TRACE_START_V1|5",
    "KSS_TRACE_V1|1|scpu|0|008000|1|1|1",
    "KSS_TRACE_V1|2|sa1|0|008100|1|1|1",
    "KSS_TRACE_V1|3|scpu|2|008001|1|1|1",
    "KSS_TRACE_V1|4|sa1|1|008100|1|1|1",
    "KSS_TRACE_V1|5|scpu|5|C08000|0|1|1",
    "KSS_TRACE_END_V1|5|limit",
]


def test_imports_dual_cpu_blocks_and_per_cpu_edges() -> None:
    result = import_lines(SYNTHETIC_TRACE)
    assert result["capture"] == {
        "limit": 5, "event_count": 5, "end_reason": "limit", "bounded": True,
    }
    assert result["processors"]["scpu"] == {"events": 3, "unique_blocks": 3}
    assert result["processors"]["sa1"] == {"events": 2, "unique_blocks": 1}
    assert len(result["blocks"]) == 4
    assert sum(edge["hits"] for edge in result["edges"]) == 3
    assert all(edge["source"]["processor"] == edge["target"]["processor"] for edge in result["edges"])


def test_output_is_deterministic_and_excludes_sensitive_fields() -> None:
    first = import_lines(SYNTHETIC_TRACE)
    second = import_lines(SYNTHETIC_TRACE)
    assert first == second
    serialized = json.dumps(first, sort_keys=True)
    for forbidden in ("opcode", "value", "register", "memory", "rom_path", "source_path"):
        assert forbidden not in serialized.lower()


def test_ignores_unrelated_mesen_diagnostics() -> None:
    lines = ["loading synthetic fixture", *SYNTHETIC_TRACE, "shutdown complete"]
    assert import_lines(lines) == import_lines(SYNTHETIC_TRACE)


@pytest.mark.parametrize(
    ("lines", "message"),
    [
        (["KSS_TRACE_V1|1|scpu|0|008000|1|1|1"], "missing trace start"),
        (["KSS_TRACE_START_V1|2", "KSS_TRACE_V1|2|scpu|0|008000|1|1|1"], "non-contiguous"),
        (["KSS_TRACE_START_V1|1", "KSS_TRACE_V1|1|scpu|0|008000|1|0|1"], "emulation mode"),
        (["KSS_TRACE_START_V1|1"], "no execution events"),
        (["KSS_TRACE_START_V1|1", "KSS_TRACE_V1|1|scpu|0|008000|1|1|1", "KSS_TRACE_END_V1|2|complete"], "count does not match"),
    ],
)
def test_rejects_unsafe_or_inconsistent_trace(lines, message) -> None:
    with pytest.raises(TraceFormatError, match=message):
        import_lines(lines)


def test_rejects_per_processor_cycle_regression() -> None:
    lines = [
        "KSS_TRACE_START_V1|2",
        "KSS_TRACE_V1|1|scpu|10|008000|1|1|1",
        "KSS_TRACE_V1|2|scpu|9|008001|1|1|1",
    ]
    with pytest.raises(TraceFormatError, match="moved backwards"):
        import_lines(lines)


def test_cli_writes_only_sanitized_json(tmp_path: Path) -> None:
    source = tmp_path / "synthetic.log"
    output = tmp_path / "coverage.json"
    source.write_text("\n".join(SYNTHETIC_TRACE), encoding="utf-8")
    assert main([str(source), str(output)]) == 0
    assert json.loads(output.read_text(encoding="utf-8")) == import_lines(SYNTHETIC_TRACE)


def test_sanitized_output_validates_against_schema() -> None:
    root = Path(__file__).resolve().parents[2]
    schema = json.loads((root / "schemas" / "trace" / "coverage.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(import_lines(SYNTHETIC_TRACE))
