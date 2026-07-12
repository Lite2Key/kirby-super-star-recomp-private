"""Emit value-free first-reset-block metadata from a private differential log."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from .compare import PrivateLog, parse_log


BLOCKS = {
    "scpu": {"start": 0x008004, "terminator": 0x008170, "end": 0x00816D},
    "sa1": {"start": 0x008BF4, "terminator": 0x008C20, "end": 0x008C23},
}


def _digest(value: object) -> str:
    encoded = json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _block(log: PrivateLog, processor: str, spec: dict[str, int]) -> dict[str, Any]:
    states = [state for state in log.states if state[1] == processor]
    start_index = next((i for i, state in enumerate(states) if state[3] == spec["start"]), None)
    if start_index is None:
        raise ValueError(f"{processor} reset start is absent")
    end_index = None
    for index in range(start_index + 1, len(states)):
        if states[index - 1][3] == spec["terminator"] and states[index][3] == spec["end"]:
            end_index = index
            break
    if end_index is None:
        raise ValueError(f"{processor} first reset block has no observed terminating edge")
    chain = states[start_index : end_index + 1]
    start_ordinal = start_index + 1
    end_ordinal = end_index + 1
    writes = [
        write for write in log.writes
        if write[0] == processor
        and write[1] is not None
        and start_ordinal <= write[1] < end_ordinal
    ]
    return {
        "processor": processor,
        "start_pc": spec["start"],
        "terminator_pc": spec["terminator"],
        "end_pc": spec["end"],
        "instruction_records": end_index - start_index,
        "state_records": len(chain),
        "cycle_start": chain[0][2],
        "cycle_end": chain[-1][2],
        "cycle_delta": chain[-1][2] - chain[0][2],
        "write_records": len(writes),
        # The raw sequence number is global across two asynchronously
        # interleaved CPUs. Exclude it so a deterministic per-CPU candidate
        # runner can reproduce the architectural chain exactly.
        "state_chain_sha256": _digest([state[1:] for state in chain]),
        "write_chain_sha256": _digest(writes),
    }


def summarize_reset_blocks(path: Path) -> dict[str, Any]:
    log = parse_log(path)
    return {
        "schema_version": 1,
        "source_format": "kss-differential-v1",
        "source_sha256": log.digest,
        "blocks": [_block(log, processor, spec) for processor, spec in BLOCKS.items()],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_reset_blocks(args.input), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing reset-block summary: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
