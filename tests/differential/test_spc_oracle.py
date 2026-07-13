from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from differential.kss_diff.spc_oracle import SpcOracleError, summarize_spc_oracle


ROOT = Path(__file__).resolve().parents[2]


def raw() -> str:
    return "\n".join([
        "Mesen banner",
        "KSS_SPC_START_V1|100000|10000",
        "KSS_SPC_EXEC_V1|1|2|FFC5|198|1|2|3|239|2",
        "KSS_SPC_PORT_V1|1|spc_to_cpu|3|0|170",
        "KSS_SPC_EXEC_V1|2|10|FFC6|29|1|2|3|239|2",
        "KSS_SPC_FINAL_V1|12|FFC7|1|2|3|239|2",
        "KSS_SPC_END_V1|first_end_frame|2|1|100|12",
        "",
    ])


def raw_v2() -> str:
    return "\n".join([
        "Mesen banner",
        "KSS_SPC_START_V2|100000|20000",
        "KSS_SPC_EXEC_V2|1|1|20|2|FFC5|198|1|2|3|239|2",
        "KSS_SPC_IO_V2|2|1|spc_port_read|20|3|9|0|170",
        "KSS_SPC_IO_V2|3|2|scpu_port_write|20|3|10|0|187",
        "KSS_SPC_EXEC_V2|4|2|24|10|FFC6|29|1|2|3|239|2",
        "KSS_SPC_FINAL_V2|5|100|12|FFC7|1|2|3|239|2",
        "KSS_SPC_END_V2|first_end_frame|2|2|5|100|12",
        "",
    ])


def test_value_free_inventory_and_digests(tmp_path: Path) -> None:
    path = tmp_path / "spc.log"
    path.write_text(raw(), encoding="utf-8")
    result = summarize_spc_oracle(path)
    assert result["execution"]["records"] == 2
    assert result["execution"]["opcodes"] == [
        {"opcode": 29, "records": 1}, {"opcode": 198, "records": 1}
    ]
    assert result["ports"]["records"] == 1
    rendered = json.dumps(result)
    for key in ('"a"', '"x"', '"y"', '"sp"', '"ps"', '"value"'):
        assert key not in rendered


def test_v2_phase_inventory_is_strict_and_value_free(tmp_path: Path) -> None:
    path = tmp_path / "spc-v2.log"
    path.write_text(raw_v2(), encoding="utf-8")
    result = summarize_spc_oracle(path)
    assert result["schema_version"] == 2
    assert result["execution"]["records"] == 2
    assert result["io"]["records"] == 2
    assert result["phase_order"]["records"] == 5
    assert result["phase_order"]["same_master_clock_groups"] == 1
    rendered = json.dumps(result)
    for key in ('"a"', '"x"', '"y"', '"sp"', '"ps"', '"value"'):
        assert key not in rendered


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ("EXEC_V2|4|2", "EXEC_V2|5|2", "event ordinals"),
        ("IO_V2|3|2", "IO_V2|3|3", "I/O ordinals"),
        ("IO_V2|2|1|spc_port_read|20", "IO_V2|2|1|spc_port_read|21", "master clocks"),
        ("FINAL_V2|5|100", "FINAL_V2|6|100", "final event ordinal"),
        ("first_end_frame|2|2|5", "first_end_frame|2|2|6", "event count"),
    ],
)
def test_v2_corruption_is_rejected(
    tmp_path: Path, old: str, new: str, message: str
) -> None:
    path = tmp_path / "bad-v2.log"
    path.write_text(raw_v2().replace(old, new), encoding="utf-8")
    with pytest.raises(SpcOracleError, match=message):
        summarize_spc_oracle(path)


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ("EXEC_V1|2|10", "EXEC_V1|3|10", "ordinals"),
        ("PORT_V1|1|spc_to_cpu", "PORT_V1|2|spc_to_cpu", "port ordinals"),
        ("first_end_frame|2|1", "first_end_frame|3|1", "exec count"),
        ("FINAL_V1|12", "FINAL_V1|13", "final cycle"),
        ("KSS_SPC_END_V1", "KSS_SPC_ABORT_V1", "aborted"),
    ],
)
def test_corruption_is_rejected(tmp_path: Path, old: str, new: str, message: str) -> None:
    path = tmp_path / "bad.log"
    path.write_text(raw().replace(old, new), encoding="utf-8")
    with pytest.raises(SpcOracleError, match=message):
        summarize_spc_oracle(path)


def test_committed_spc_reference_is_strict() -> None:
    result = json.loads(
        (ROOT / "analysis/differential/spc-first-frame-reference.json").read_text(encoding="utf-8")
    )
    schema = json.loads(
        (ROOT / "schemas/differential/spc-first-frame.schema.json").read_text(encoding="utf-8")
    )
    jsonschema.validate(result, schema)
    assert result["execution"]["records"] == 3661
    assert len(result["execution"]["opcodes"]) == 17
    assert result["ports"]["records"] == 462


def test_mesen_harness_is_bounded_and_resets_after_hooks() -> None:
    script = (ROOT / "tools/mesen/spc_first_frame_oracle.lua").read_text(encoding="utf-8")
    for required in (
        "MAX_EXEC", "MAX_IO", "emu.cpuType.spc", "emu.memType.spcMemory",
        "emu.eventType.endFrame", "emu.callbackType.exec", "emu.callbackType.write",
        "emu.callbackType.read", "emu.getMasterClock()", "KSS_SPC_EXEC_V2",
        "KSS_SPC_IO_V2", "event_ordinal", "emu.reset()",
    ):
        assert required in script
