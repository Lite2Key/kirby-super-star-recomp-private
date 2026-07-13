from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def _identity(item: dict) -> tuple[int, bool, bool, bool]:
    mode = item["mode"]
    return item["pc"], mode["emulation"], mode["m8"], mode["x8"]


def test_committed_scpu_reference_identity_coverage_is_complete_and_value_free() -> None:
    path = ROOT / "analysis/differential/scpu-reference-identity-coverage.json"
    result = json.loads(path.read_text(encoding="utf-8"))
    schema = json.loads(
        (ROOT / "schemas/differential/scpu-reference-identity-coverage.schema.json")
        .read_text(encoding="utf-8")
    )
    jsonschema.validate(result, schema)

    assert result["inventory_count"] == result["reference_verified_count"] == 214
    assert result["newly_reference_verified_count"] == 54
    assert len(result["newly_reference_verified_identities"]) == 54
    assert not result["remaining_unverified_identities"]
    assert result["rom_bytes_included"] is False
    assert result["private_values_included"] is False


def test_new_reference_identities_are_exactly_the_post_reset_scpu_tail() -> None:
    coverage = json.loads(
        (ROOT / "analysis/differential/scpu-reference-identity-coverage.json")
        .read_text(encoding="utf-8")
    )
    trace = json.loads(
        (ROOT / "analysis/cfg/first-frame-dual.trace-cfg.json").read_text(encoding="utf-8")
    )
    reset = json.loads(
        (ROOT / "analysis/differential/reset-block-reference.json").read_text(encoding="utf-8")
    )
    reset_window = next(block for block in reset["blocks"] if block["processor"] == "scpu")
    observations = [
        item for item in trace["observations"]["blocks"]
        if item["identity"]["processor"] == "scpu"
    ]
    expected = {
        _identity(item["identity"])
        for item in observations
        if not reset_window["cycle_start"] <= item["first_cycle"] <= reset_window["cycle_end"]
    }
    actual = {_identity(item) for item in coverage["newly_reference_verified_identities"]}

    assert actual == expected
    assert sum(window["new_identity_count"] for window in coverage["proof_windows"]) == 214
    assert [window["identity_count"] for window in coverage["proof_windows"]] == [160, 17, 38]


def test_reference_identity_artifact_contains_no_architectural_values() -> None:
    coverage = json.loads(
        (ROOT / "analysis/differential/scpu-reference-identity-coverage.json")
        .read_text(encoding="utf-8")
    )
    rendered = json.dumps(coverage, sort_keys=True)
    for forbidden in ('"a"', '"x"', '"y"', '"d"', '"sp"', '"dbr"', '"ps"',
                      '"writes"', '"values"', '"bytes"'):
        assert forbidden not in rendered
