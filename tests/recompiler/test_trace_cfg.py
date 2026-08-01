import copy
import json
from pathlib import Path

import jsonschema
import pytest

from recompiler.kssrecomp.trace_cfg import TraceCfgError, build_trace_seeded_cfg, main


ROOT = Path(__file__).resolve().parents[2]


def ident(processor: str, pc: int, *, emulation: bool = True) -> dict[str, object]:
    return {
        "processor": processor,
        "pc": pc,
        "mode": {"emulation": emulation, "m8": True, "x8": True},
    }


def coverage() -> dict[str, object]:
    scpu0, scpu1 = ident("scpu", 0x008000), ident("scpu", 0x008001)
    sa10, sa11 = ident("sa1", 0x000100), ident("sa1", 0x000101, emulation=False)
    return {
        "schema_version": 1,
        "source_format": "mesen-ce-kss-trace-v1",
        "capture": {"limit": 6, "event_count": 6, "end_reason": "limit", "bounded": True},
        "processors": {
            "scpu": {"events": 4, "unique_blocks": 2},
            "sa1": {"events": 2, "unique_blocks": 2},
        },
        "blocks": [
            {**scpu1, "hits": 3, "first_cycle": 12, "last_cycle": 20},
            {**sa11, "hits": 1, "first_cycle": 7, "last_cycle": 7},
            {**scpu0, "hits": 1, "first_cycle": 10, "last_cycle": 10},
            {**sa10, "hits": 1, "first_cycle": 4, "last_cycle": 4},
        ],
        "edges": [
            {"source": scpu0, "target": scpu1, "hits": 1},
            {"source": sa10, "target": sa11, "hits": 1},
        ],
    }


def test_builds_dual_cpu_cfg_and_earliest_reset_seeds() -> None:
    result = build_trace_seeded_cfg(coverage())
    assert [(e["identity"]["processor"], e["identity"]["pc"]) for e in result["entries"]] == [
        ("scpu", 0x008000), ("sa1", 0x000100)
    ]
    blocks = result["cfg"]["blocks"]
    assert [(b["identity"]["processor"], b["identity"]["pc"]) for b in blocks] == [
        ("sa1", 0x000100), ("sa1", 0x000101), ("scpu", 0x008000), ("scpu", 0x008001)
    ]
    assert all(block["instructions"] == [] for block in blocks)
    assert blocks[0]["edges"][0]["target"]["mode"]["emulation"] is False
    assert blocks[2]["edges"][0]["kind"] == "observed"


def test_output_is_deterministic_and_validates_against_schema(tmp_path: Path) -> None:
    source = tmp_path / "coverage.json"
    output = tmp_path / "cfg.json"
    source.write_text(json.dumps(coverage()), encoding="utf-8")
    assert main([str(source), str(output)]) == 0
    first = output.read_bytes()
    assert main([str(source), str(output)]) == 0
    assert output.read_bytes() == first
    assert main([str(source), str(output), "--check"]) == 0
    schema = json.loads((ROOT / "schemas/recompiler/trace-seeded-cfg.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(json.loads(first))


@pytest.mark.parametrize("mutation", ["cross_cpu", "missing_block", "summary", "bad_mode"])
def test_rejects_inconsistent_sanitized_coverage(mutation: str) -> None:
    value = copy.deepcopy(coverage())
    if mutation == "cross_cpu":
        value["edges"][0]["target"] = value["edges"][1]["target"]
    elif mutation == "missing_block":
        value["blocks"].pop()
    elif mutation == "summary":
        value["processors"]["scpu"]["events"] = 99
    else:
        value["blocks"][0]["mode"] = {"emulation": True, "m8": False, "x8": True}
    with pytest.raises(TraceCfgError):
        build_trace_seeded_cfg(value)


def test_real_sanitized_capture_emits_schema_valid_rom_free_artifact() -> None:
    source = json.loads((ROOT / "analysis/coverage/bootstrap-dual.json").read_text())
    result = build_trace_seeded_cfg(source)
    schema = json.loads((ROOT / "schemas/recompiler/trace-seeded-cfg.schema.json").read_text())
    jsonschema.Draft202012Validator(schema).validate(result)
    assert {entry["identity"]["processor"] for entry in result["entries"]} == {"scpu", "sa1"}
    rendered = json.dumps(result)
    assert "opcode" not in rendered and "operand_hex" not in rendered
    committed = json.loads((ROOT / "analysis/cfg/bootstrap-dual.trace-cfg.json").read_text())
    assert committed == result


def test_first_visible_route_converts_all_identities_without_rom_values() -> None:
    source = json.loads(
        (ROOT / "analysis/coverage/first-visible-route-coverage.json").read_text()
    )
    result = build_trace_seeded_cfg(source)
    assert len(result["cfg"]["blocks"]) == 2273
    assert sum(len(block["edges"]) for block in result["cfg"]["blocks"]) == 2440
    assert result["processors"] == {
        "scpu": {"events": 1271467, "unique_blocks": 1032},
        "sa1": {"events": 3299911, "unique_blocks": 1241},
    }
    rendered = json.dumps(result)
    assert all(token not in rendered for token in (
        "opcode", "bytes_hex", "operand_hex", "mnemonic", "rom_offset"
    ))
    committed = json.loads(
        (ROOT / "analysis/cfg/first-visible-route.trace-cfg.json").read_text()
    )
    assert committed == result
