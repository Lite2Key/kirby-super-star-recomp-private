from __future__ import annotations

import copy
import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.generated_blocks import GeneratedBlocksError, render


ROOT = Path(__file__).resolve().parents[2]


def _document() -> dict:
    return json.loads((ROOT / "analysis/cfg/bootstrap-dual.lifted-reset.json").read_text())


def test_real_reset_artifact_emits_bounded_deterministic_cpp() -> None:
    first = render(_document(), max_blocks_per_processor=256)
    second = render(_document(), max_blocks_per_processor=256)
    assert first == second
    header, source, manifest = first
    assert "register_reset_blocks" in header
    assert manifest["processors"] == {"scpu": 160, "sa1": 23}
    assert manifest["registered_blocks"] == 183
    assert "block_scpu_008004_e1m1x1" in source
    assert "block_sa1_008bf4_e1m1x1" in source
    assert "block_sa1_008c20_e0m0x0" in source  # restartable MVN self-loop
    assert "cpu.block_key() != expected" in source
    assert "cpu.stopped = true" in source
    assert "observe_generated_instruction_fetches(" in source
    assert "expected.address, instruction.operand_count" in source
    schema = json.loads((ROOT / "schemas/recompiler/generated-block-manifest.schema.json").read_text())
    jsonschema.validate(manifest, schema)


def test_cross_processor_edges_and_duplicates_fail_closed() -> None:
    document = _document()
    broken = copy.deepcopy(document)
    broken["edges"][0]["target"]["processor"] = "scpu"
    with pytest.raises(GeneratedBlocksError, match="crosses processors"):
        render(broken)
    broken = copy.deepcopy(document)
    broken["blocks"].append(copy.deepcopy(broken["blocks"][0]))
    with pytest.raises(GeneratedBlocksError, match="duplicate"):
        render(broken)


def test_limit_is_validated() -> None:
    with pytest.raises(GeneratedBlocksError, match="between 1 and 4096"):
        render(_document(), max_blocks_per_processor=0)


def test_manifest_counts_registered_identity_route_provenance() -> None:
    document = _document()
    document["blocks"][0]["routes"] = ["boot", "first-visible"]
    manifest = render(document, max_blocks_per_processor=256)[2]
    assert manifest["route_provenance"] == {
        "policy": "coverage-route-ids-per-registered-identity",
        "registered_blocks_by_route": {"boot": 1, "first-visible": 1},
    }
    schema = json.loads((ROOT / "schemas/recompiler/generated-block-manifest.schema.json").read_text())
    jsonschema.validate(manifest, schema)


def test_all_observed_selection_registers_decoded_nodes_beyond_a_frontier() -> None:
    document = _document()
    document["source"]["selection_policy"] = "all-observed-decoded"
    document["edges"] = []
    manifest = render(document, max_blocks_per_processor=4096)[2]
    expected = sum(
        block["status"] == "decoded" and block["instruction"] is not None
        for block in document["blocks"]
    )
    assert manifest["registered_blocks"] == expected


def test_all_observed_leaf_blocks_stop_before_disconnected_registered_nodes() -> None:
    document = _document()
    document["source"]["selection_policy"] = "all-observed-decoded"
    document["edges"] = []
    source = render(document, max_blocks_per_processor=4096)[1]
    expected = sum(
        block["status"] == "decoded" and block["instruction"] is not None
        for block in document["blocks"]
    )
    assert source.count("    cpu.stopped = true;\n}") == expected
