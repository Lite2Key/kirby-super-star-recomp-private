from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.frontier import FrontierError, build_frontier

ROOT = Path(__file__).resolve().parents[2]

def _load(path: str) -> dict:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))

def test_first_frame_frontier_is_exact_and_schema_valid() -> None:
    result = build_frontier(
        _load("analysis/coverage/first-frame-dual.json"),
        _load("analysis/cfg/first-frame-dual.lifted.json"),
        _load("analysis/cfg/bootstrap-dual.lifted-reset.json"),
    )
    assert result["generated_nodes"] == 254
    assert result["capture"] == {"event_count": 25087, "scpu_events": 8533, "sa1_events": 16554}
    assert result["processors"]["scpu"]["reachable_nodes"] == 214
    assert result["processors"]["scpu"]["new_nodes"] == 54
    assert result["processors"]["scpu"]["first_unsupported"] is None
    assert result["processors"]["scpu"]["unsupported_opcodes"] == []
    assert result["processors"]["sa1"]["reachable_nodes"] == 40
    assert result["processors"]["sa1"]["new_nodes"] == 17
    assert result["processors"]["sa1"]["first_unsupported"] is None
    assert result["unresolved"] == {"scpu": [], "sa1": []}
    jsonschema.validate(result, _load("schemas/recompiler/frontier.schema.json"))

def test_incomplete_capture_is_rejected() -> None:
    coverage = _load("analysis/coverage/first-frame-dual.json")
    coverage["capture"]["end_reason"] = "limit"
    with pytest.raises(FrontierError, match="observed boundary"):
        build_frontier(
            coverage,
            _load("analysis/cfg/first-frame-dual.lifted.json"),
            _load("analysis/cfg/bootstrap-dual.lifted-reset.json"),
        )
