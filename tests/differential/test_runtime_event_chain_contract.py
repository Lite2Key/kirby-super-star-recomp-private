from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def load(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def test_event_chain_expectation_is_valid_and_source_anchored() -> None:
    expectation = load("analysis/differential/first-endframe-event-chain-expectation.json")
    schema = load("schemas/differential/first-endframe-event-chain-expectation.schema.json")
    jsonschema.validate(expectation, schema)

    frame = load("analysis/differential/first-frame-reference.json")
    spc = load("analysis/differential/spc-first-frame-reference.json")
    chains = expectation["chains"]
    expected = {
        "cpu_writes": frame["writes"],
        "scpu_writes": frame["writes"]["processors"][0],
        "sa1_writes": frame["writes"]["processors"][1],
        "ppu_register_writes": frame["register_events"][0],
        "dma_register_writes": frame["register_events"][1],
        "spc_ports": spc["ports"],
    }
    for name, source in expected.items():
        records = source["total_records"] if "total_records" in source else source["records"]
        digest = source.get("chain_sha256")
        assert chains[name] == {"records": records, "sha256": digest}


def test_event_chain_expectation_contains_no_private_records() -> None:
    path = ROOT / "analysis/differential/first-endframe-event-chain-expectation.json"
    rendered = path.read_text(encoding="utf-8")
    assert "sanitized-digests-and-counts-only" in rendered
    assert '"available": false' in rendered
    for forbidden in ("value", "address", "cycle", "trace_records", "bytes_hex"):
        assert forbidden not in rendered
