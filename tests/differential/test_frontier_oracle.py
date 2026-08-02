from __future__ import annotations

import json
from pathlib import Path

import jsonschema

from differential.kss_diff.frontier_oracle import summarize_frontier, summarize_second_frontier


ROOT = Path(__file__).resolve().parents[2]


def test_frontier_summary_is_value_free_and_strict(tmp_path: Path) -> None:
    raw = "\n".join((
        "KSS_FRONTIER_START_V1|008172",
        "KSS_FRONTIER_STATE_V1|1|10|008172|1|2|3|4|5|0|0|6|0|33138",
        "KSS_FRONTIER_WRITE_V1|1|11|001FFF|99",
        "KSS_FRONTIER_STATE_V1|2|18|00D66E|7|8|9|10|11|213|0|12|0|54894",
        "KSS_FRONTIER_END_V1|2", "",
    ))
    source = tmp_path / "frontier.log"
    source.write_text(raw, encoding="utf-8")
    result = summarize_frontier(source)
    assert result["instruction_records"] == 1 and result["stack_write_records"] == 1
    rendered = json.dumps(result)
    assert "001FFF" not in rendered
    assert "states" not in result and "writes" not in result and "values" not in result
    schema = json.loads((ROOT / "schemas/differential/scpu-frontier-summary.schema.json").read_text())
    jsonschema.validate(result, schema)


def test_committed_frontier_reference_is_schema_valid() -> None:
    result = json.loads((ROOT / "analysis/differential/scpu-frontier-reference.json").read_text())
    schema = json.loads((ROOT / "schemas/differential/scpu-frontier-summary.schema.json").read_text())
    jsonschema.validate(result, schema)
    assert result["instruction_records"] == 16
    assert result["write_records"] == 11
    assert result["cycle_delta"] == 58


def test_committed_second_frontier_reference_is_schema_valid() -> None:
    result = json.loads((ROOT / "analysis/differential/scpu-frontier2-reference.json").read_text())
    schema = json.loads((ROOT / "schemas/differential/scpu-frontier2-summary.schema.json").read_text())
    jsonschema.validate(result, schema)
    assert result["instruction_records"] == 73
    assert result["cycle_delta"] == 246 and result["write_records"] == 7
