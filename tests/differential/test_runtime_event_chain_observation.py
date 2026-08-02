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
        assert chain["matches"] == (
            chain["runtime_records"] == chain["reference_records"]
            and chain["runtime_sha256"] == expectation["chains"][name]["sha256"]
        )
    assert observation["chains"]["ppu_register_writes"]["matches"]
    assert sum(chain["matches"] for chain in observation["chains"].values()) == 1
    directions = observation["spc_directions"]
    assert sum(item["runtime_records"] for item in directions.values()) == (
        observation["chains"]["spc_ports"]["runtime_records"]
    ) == 463
    assert sum(item["reference_records"] for item in directions.values()) == 462
    assert directions["cpu_to_spc"] == {
        "runtime_records": 308,
        "reference_records": 308,
        "delta": 0,
    }
    assert directions["spc_to_cpu"] == {
        "runtime_records": 155,
        "reference_records": 154,
        "delta": 1,
    }
    # The current evidence boundary isolates one extra output-latch
    # acknowledgement after an otherwise exact 462-event prefix. Keep this
    # directional assertion explicit so a future change cannot silently turn
    # the known microphase gap into a broad SPC count drift.
    assert observation["chains"]["spc_ports"]["delta"] == 1
    assert observation["cross_domain_order_records"] == (
        observation["chains"]["cpu_writes"]["runtime_records"]
        + observation["chains"]["spc_ports"]["runtime_records"]
    ) == 27369


def test_runtime_event_chain_observation_contains_no_private_records() -> None:
    rendered = (ROOT / "analysis/differential/runtime-first-endframe-event-chain-observation.json").read_text(encoding="utf-8")
    for forbidden in ('"address"', '"value"', 'trace_records', 'bytes_hex', 'spc_ipl'):
        assert forbidden not in rendered
