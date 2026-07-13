import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def identity(item: dict) -> tuple:
    mode = item["mode"]
    return (
        item["processor"], item["pc"],
        mode["emulation"], mode["m8"], mode["x8"],
    )


def test_boot_probe_identity_coverage_is_exact_and_value_free() -> None:
    report = json.loads(
        (ROOT / "analysis/coverage/boot-probe-identity-coverage.json").read_text()
    )
    inventory = json.loads(
        (ROOT / report["inventory_source"]).read_text()
    )
    inventory_set = {identity(block) for block in inventory["blocks"]}
    missing_set = {identity(block) for block in report["missing_identities"]}
    executed_set = inventory_set - missing_set

    assert len(inventory_set) == report["inventory_count"] == 254
    assert len(missing_set) == report["missing_count"] == 18
    assert missing_set <= inventory_set
    assert len(executed_set) == report["executed_count"] == 236
    for processor in ("scpu", "sa1"):
        counts = report["processors"][processor]
        assert sum(item[0] == processor for item in inventory_set) == counts["inventory"]
        assert sum(item[0] == processor for item in executed_set) == counts["executed"]
        assert sum(item[0] == processor for item in missing_set) == counts["missing"]

    waits = report["bounded_wait_observations"]
    assert identity(waits["sa1"]["checkpoint"]) == (
        "sa1", 0x008C58, False, False, False
    )
    assert waits["sa1"]["completed_blocks"] == 2
    assert identity(waits["scpu"]["checkpoint"]) == (
        "scpu", 0x00D68E, False, True, False
    )
    assert waits["scpu"]["completed_blocks"] == 20
    assert {waits[processor]["result"] for processor in waits} == {
        "returned-to-same-identity"
    }
    assert "not present in bounded execution" in waits["sa1"]["release_dependency"]
    assert "not present in bounded execution" in waits["scpu"]["release_dependency"]

    rendered = json.dumps(report)
    for forbidden in ("bytes_hex", "rom_offset", "opcode", "mnemonic", "operand"):
        assert forbidden not in rendered
    assert "not architectural-state, event, timing, or reference parity" in report["claim_scope"]
