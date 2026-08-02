from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def test_first_visible_frame_reference_is_schema_valid_and_repeat_stable() -> None:
    path = ROOT / "analysis/differential/first-visible-frame-reference.json"
    reference = json.loads(path.read_text(encoding="utf-8"))
    schema = json.loads(
        (ROOT / "schemas/differential/first-visible-frame-reference.schema.json").read_text(
            encoding="utf-8"
        )
    )
    jsonschema.validate(reference, schema)
    assert reference["repeat_captures"] >= 2
    assert reference["master_clock"] > 306900
    assert reference["nonzero_pixels"] > 0
    assert len(set(reference["scpu_cycle_observations"])) > 1


def test_first_visible_frame_reference_contains_no_private_pixels_or_paths() -> None:
    rendered = (
        ROOT / "analysis/differential/first-visible-frame-reference.json"
    ).read_text(encoding="utf-8")
    for forbidden in (".private", "pixels_hex", "argb_chunk", '"rom_bytes":', "C:\\\\Users"):
        assert forbidden not in rendered
