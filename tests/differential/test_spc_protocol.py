from __future__ import annotations

from pathlib import Path

import pytest

from differential.kss_diff.spc_protocol import SpcProtocolError, summarize_ipl_upload_protocol


def capture(*, corrupt_ack_pc: bool = False, omit_echo: bool = False) -> str:
    ack_pc = "FFCE" if corrupt_ack_pc else "FFCF"
    records = [
        "KSS_SPC_EXEC_V1|1|10|FFC9|143|0|0|0|239|2",
        "KSS_SPC_PORT_V1|1|spc_to_cpu|20|0|170",
        "KSS_SPC_EXEC_V1|2|20|FFCC|143|0|0|0|239|2",
        "KSS_SPC_PORT_V1|2|spc_to_cpu|30|1|187",
        "KSS_SPC_PORT_V1|3|cpu_to_spc|100|2|0",
        "KSS_SPC_PORT_V1|4|cpu_to_spc|101|3|7",
        "KSS_SPC_PORT_V1|5|cpu_to_spc|102|1|1",
        "KSS_SPC_EXEC_V1|3|1000|FFCF|120|0|0|0|239|0",
        "KSS_SPC_PORT_V1|6|cpu_to_spc|103|0|204",
        "KSS_SPC_EXEC_V1|4|1010|FFD2|208|0|0|0|239|0",
        f"KSS_SPC_EXEC_V1|5|1018|{ack_pc}|120|0|0|0|239|0",
        "KSS_SPC_EXEC_V1|6|1028|FFD2|208|0|0|0|239|3",
        "KSS_SPC_EXEC_V1|7|1032|FFD4|47|0|0|0|239|3",
        "KSS_SPC_EXEC_V1|8|1040|FFEF|186|0|0|0|239|3",
        "KSS_SPC_EXEC_V1|9|1050|FFF1|218|0|0|7|239|1",
        "KSS_SPC_EXEC_V1|10|1060|FFF3|186|0|0|7|239|1",
        "KSS_SPC_EXEC_V1|11|1070|FFF5|196|204|0|1|239|1",
        "KSS_SPC_PORT_V1|7|spc_to_cpu|1078|0|204",
        "KSS_SPC_PORT_V1|8|cpu_to_spc|200|0|0",
        "KSS_SPC_PORT_V1|9|cpu_to_spc|201|1|99",
        "KSS_SPC_PORT_V1|10|cpu_to_spc|202|0|1",
        "KSS_SPC_PORT_V1|11|cpu_to_spc|203|1|88",
    ]
    if not omit_echo:
        records.append("KSS_SPC_PORT_V1|12|spc_to_cpu|1100|0|0")
    return "\n".join(records) + "\n"


def test_extracts_ack_path_without_payload_values(tmp_path: Path) -> None:
    path = tmp_path / "spc.log"
    path.write_text(capture(), encoding="utf-8")
    result = summarize_ipl_upload_protocol(path)
    assert result["start_acknowledgement"]["instruction_path"][0]["pc"] == 0xFFD2
    assert result["start_acknowledgement"]["instruction_path"][-1]["pc"] == 0xFFF5
    assert result["start_acknowledgement"]["architectural_spc_cycles_from_first_post_write_boundary"] == 34
    assert result["transfer_at_boundary"] == {
        "bytes_sent": 2,
        "counters_echoed": 1,
        "pending_counter": 1,
        "payload_values_included": False,
    }
    assert result["transfer_at_boundary"]["payload_values_included"] is False


def test_rejects_wrong_acknowledgement_control_flow(tmp_path: Path) -> None:
    path = tmp_path / "bad.log"
    path.write_text(capture(corrupt_ack_pc=True), encoding="utf-8")
    with pytest.raises(SpcProtocolError, match="instruction path"):
        summarize_ipl_upload_protocol(path)


def test_rejects_more_than_one_pending_counter(tmp_path: Path) -> None:
    path = tmp_path / "pending.log"
    path.write_text(capture(omit_echo=True), encoding="utf-8")
    with pytest.raises(SpcProtocolError, match="more than one"):
        summarize_ipl_upload_protocol(path)
