from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def test_runtime_event_chain_observation_is_valid_and_reference_anchored() -> None:
    observation = load("analysis/differential/runtime-first-endframe-event-chain-observation.json")
    schema = load("schemas/differential/runtime-first-endframe-event-chain-observation.schema.json")
    expectation = load("analysis/differential/first-endframe-event-chain-expectation.json")
    jsonschema.validate(observation, schema)

    for name, chain in observation["chains"].items():
        assert chain["reference_records"] == expectation["chains"][name]["records"]
        assert chain["delta"] == chain["runtime_records"] - chain["reference_records"]
        assert not chain["matches"]
    directions = observation["spc_directions"]
    assert sum(item["runtime_records"] for item in directions.values()) == (
        observation["chains"]["spc_ports"]["runtime_records"]
    ) == 465
    assert sum(item["reference_records"] for item in directions.values()) == 462


def test_runtime_event_chain_observation_contains_no_private_records() -> None:
    rendered = (ROOT / "analysis/differential/runtime-first-endframe-event-chain-observation.json").read_text(encoding="utf-8")
    for forbidden in ('"address"', '"value"', 'trace_records', 'bytes_hex', 'spc_ipl'):
        assert forbidden not in rendered
