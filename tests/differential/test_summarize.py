from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from tools.differential.kss_diff.summarize import summarize_reset_blocks


ROOT = Path(__file__).resolve().parents[2]


def private_log() -> str:
    return "\n".join((
        "KSS_DIFF_START_V1|4",
        "KSS_DIFF_STATE_V1|1|scpu|0|008004|0|0|0|0|511|0|52|1",
        "KSS_DIFF_WRITE_V1|scpu|1|1|000100|7",
        "KSS_DIFF_WRITE_V1|scpu|1|1|00420B|2",
        "KSS_DIFF_WRITE_V1|scpu|2|2|002180|44",
        "KSS_DIFF_WRITE_V1|scpu|2|2|7E0010|44",
        "KSS_DIFF_STATE_V1|2|scpu|3|008170|0|0|0|0|511|0|0|0",
        "KSS_DIFF_STATE_V1|3|scpu|6|00816D|0|0|0|0|511|0|0|0",
        "KSS_DIFF_STATE_V1|4|sa1|10|008BF4|0|0|0|0|511|0|52|1",
        "KSS_DIFF_STATE_V1|5|sa1|17|008C20|0|0|0|0|511|0|0|0",
        "KSS_DIFF_WRITE_V1|sa1|2|18|000200|9",
        "KSS_DIFF_STATE_V1|6|sa1|24|008C23|0|0|0|0|511|0|0|0",
        "KSS_DIFF_END_V1|6|limit|3|3",
        "",
    ))


def test_summary_is_value_free_deterministic_and_schema_valid(tmp_path: Path) -> None:
    source = tmp_path / "private.log"
    source.write_text(private_log(), encoding="utf-8")
    result = summarize_reset_blocks(source)
    assert result == summarize_reset_blocks(source)
    assert [(block["processor"], block["instruction_records"], block["write_records"]) for block in result["blocks"]] == [
        ("scpu", 2, 4), ("sa1", 2, 1)
    ]
    assert result["dma_groups"] == [{
        "trigger_instruction_ordinal": 1,
        "callback_instruction_ordinal": 2,
        "trigger_pc": 0x008004,
        "port_write_records": 1,
        "wram_write_records": 1,
        "wram_first_address": 0x7E0010,
        "wram_last_address": 0x7E0010,
        "write_chain_sha256": result["dma_groups"][0]["write_chain_sha256"],
    }]
    rendered = json.dumps(result)
    assert "register" not in rendered and "000100" not in rendered
    schema = json.loads((ROOT / "schemas/differential/reset-block-summary.schema.json").read_text())
    jsonschema.validate(result, schema)


def test_missing_terminating_edge_is_rejected(tmp_path: Path) -> None:
    source = tmp_path / "private.log"
    source.write_text(private_log().replace("00816D", "008172"), encoding="utf-8")
    with pytest.raises(ValueError, match="S-CPU|scpu"):
        summarize_reset_blocks(source)
