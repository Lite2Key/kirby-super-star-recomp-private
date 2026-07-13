"""Sanitize the private S-CPU post-reset frontier oracle into digests."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def _digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
    ).hexdigest()


def _summarize(path: Path, *, marker: str, start_pc: int, end_pc: int,
               source_format: str) -> dict[str, Any]:
    raw = path.read_bytes()
    lines = raw.decode("utf-8", errors="strict").splitlines()
    prefix = f"KSS_{marker}"
    starts = [line for line in lines if line.startswith(f"{prefix}_START_V1|")]
    ends = [line for line in lines if line.startswith(f"{prefix}_END_V1|")]
    if starts != [f"{prefix}_START_V1|{start_pc:06X}"] or len(ends) != 1:
        raise ValueError("frontier oracle requires exact start and end markers")
    states: list[tuple[int, ...]] = []
    writes: list[tuple[int, ...]] = []
    for line in lines:
        if line.startswith(f"{prefix}_STATE_V1|"):
            parts = line.split("|")
            if len(parts) != 14:
                raise ValueError("malformed frontier state")
            state = (int(parts[1]), int(parts[2]), int(parts[3], 16),
                     *(int(item) for item in parts[4:]))
            if state[2] > 0xFFFFFF:
                raise ValueError("frontier PC is out of range")
            states.append(state)
        elif line.startswith(f"{prefix}_WRITE_V1|"):
            parts = line.split("|")
            if len(parts) != 5:
                raise ValueError("malformed frontier write")
            writes.append((int(parts[1]), int(parts[2]), int(parts[3], 16), int(parts[4])))
    if [state[0] for state in states] != list(range(1, len(states) + 1)):
        raise ValueError("frontier state ordinals are not contiguous")
    end_count = int(ends[0].split("|")[1])
    if end_count != len(states) or len(states) < 2:
        raise ValueError("frontier end count does not match states")
    if states[0][2] != start_pc or states[-1][2] != end_pc:
        raise ValueError("frontier endpoints do not match the bounded proof")
    if any(not 0 <= write[3] <= 0xFF for write in writes):
        raise ValueError("frontier write value is out of range")
    return {
        "schema_version": 1,
        "source_format": source_format,
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "start_pc": states[0][2],
        "end_pc": states[-1][2],
        "instruction_records": len(states) - 1,
        "state_records": len(states),
        "cycle_start": states[0][1],
        "cycle_end": states[-1][1],
        "cycle_delta": states[-1][1] - states[0][1],
        "write_records": len(writes),
        "stack_write_records": sum(0x001F00 <= write[2] <= 0x001FFF for write in writes),
        "direct_page_write_records": sum(write[2] < 0x0100 for write in writes),
        "state_chain_sha256": _digest(states),
        "write_chain_sha256": _digest(writes),
    }


def summarize_frontier(path: Path) -> dict[str, Any]:
    return _summarize(path, marker="FRONTIER", start_pc=0x008172,
                      end_pc=0x00D66E, source_format="kss-scpu-frontier-v1")


def summarize_second_frontier(path: Path) -> dict[str, Any]:
    return _summarize(path, marker="FRONTIER3", start_pc=0x00D66E,
                      end_pc=0x00D65A, source_format="kss-scpu-frontier2-v1")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--slice", choices=("first", "second"), default="first")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    summarizer = summarize_frontier if args.slice == "first" else summarize_second_frontier
    rendered = json.dumps(summarizer(args.input), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing frontier summary: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
