"""Sanitize the private SA-1 TCD-to-next-frontier differential slice."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


class Sa1SliceError(ValueError):
    """The private SA-1 slice is malformed or contradicts its contract."""


def _integer(text: str, label: str, maximum: int | None = None) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise Sa1SliceError(f"invalid {label}") from exc
    if value < 0 or (maximum is not None and value > maximum):
        raise Sa1SliceError(f"{label} is outside its allowed range")
    return value


def _address(text: str) -> int:
    if len(text) != 6 or any(char not in "0123456789abcdefABCDEF" for char in text):
        raise Sa1SliceError("invalid 24-bit address")
    return int(text, 16)


def _digest(value: object) -> str:
    encoded = json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _parse(path: Path) -> tuple[str, list[tuple[int, ...]], list[tuple[int, ...]], tuple[int, int, int]]:
    raw = path.read_bytes()
    try:
        lines = raw.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise Sa1SliceError("slice log is not UTF-8") from exc
    records = [line for line in lines if line.startswith("KSS_SA1_SLICE_")]
    if any(line.startswith("KSS_SA1_SLICE_ABORT_V1|") for line in records):
        raise Sa1SliceError("slice capture aborted")
    starts = [line for line in records if line.startswith("KSS_SA1_SLICE_START_V1|")]
    ends = [line for line in records if line.startswith("KSS_SA1_SLICE_END_V1|")]
    polls = [line for line in records if line.startswith("KSS_SA1_SLICE_POLL_V1|")]
    if len(starts) != 1 or len(ends) != 1 or len(polls) != 1:
        raise Sa1SliceError("slice requires one start, poll, and end marker")
    if records[0] != starts[0] or records[-1] != ends[0]:
        raise Sa1SliceError("slice markers do not bound every private record")

    start = starts[0].split("|")
    if len(start) != 3:
        raise Sa1SliceError("malformed start marker")
    max_writes = _integer(start[1], "write limit")
    max_frames = _integer(start[2], "frame limit")
    if not 1 <= max_writes <= 200_000 or not 1 <= max_frames <= 240:
        raise Sa1SliceError("capture bounds exceed sanitizer limits")

    states: list[tuple[int, ...]] = []
    writes: list[tuple[int, ...]] = []
    for line in records[1:-1]:
        parts = line.split("|")
        if parts[0] == "KSS_SA1_SLICE_STATE_V1":
            if len(parts) != 13:
                raise Sa1SliceError("malformed state record")
            state = (
                _integer(parts[1], "state ordinal"),
                _integer(parts[2], "state cycle"),
                _address(parts[3]),
                _integer(parts[4], "opcode", 0xff),
                _integer(parts[5], "A", 0xffff),
                _integer(parts[6], "X", 0xffff),
                _integer(parts[7], "Y", 0xffff),
                _integer(parts[8], "D", 0xffff),
                _integer(parts[9], "SP", 0xffff),
                _integer(parts[10], "DBR", 0xff),
                _integer(parts[11], "P", 0xff),
                _integer(parts[12], "E", 1),
            )
            if state[0] != len(states) + 1:
                raise Sa1SliceError("state ordinals are not contiguous")
            states.append(state)
        elif parts[0] == "KSS_SA1_SLICE_WRITE_V1":
            if len(parts) != 6:
                raise Sa1SliceError("malformed write record")
            write = (
                _integer(parts[1], "write ordinal"),
                _integer(parts[2], "write state ordinal"),
                _integer(parts[3], "write cycle"),
                _address(parts[4]),
                _integer(parts[5], "write value", 0xff),
            )
            if write[0] != len(writes) + 1 or write[1] > len(states):
                raise Sa1SliceError("write ordinals are inconsistent")
            writes.append(write)
        elif parts[0] != "KSS_SA1_SLICE_POLL_V1":
            raise Sa1SliceError("unknown private slice record")

    poll_parts = polls[0].split("|")
    if len(poll_parts) != 4:
        raise Sa1SliceError("malformed poll marker")
    poll = tuple(_integer(value, "poll cycle") for value in poll_parts[1:])
    if poll[2] != poll[1] - poll[0] or poll[2] <= 0:
        raise Sa1SliceError("poll-cycle delta is inconsistent")

    end = ends[0].split("|")
    if len(end) != 6 or end[1] != "next_unsupported":
        raise Sa1SliceError("malformed end marker")
    if _integer(end[2], "end state count") != len(states):
        raise Sa1SliceError("end state count disagrees with records")
    if _integer(end[3], "end write count") != len(writes) or len(writes) > max_writes:
        raise Sa1SliceError("end write count disagrees with records")
    if not states or _address(end[4]) != states[-1][2] or _integer(end[5], "end opcode", 0xff) != states[-1][3]:
        raise Sa1SliceError("end identity disagrees with final state")
    return hashlib.sha256(raw).hexdigest(), states, writes, poll


def summarize_sa1_slice(path: Path) -> dict[str, Any]:
    source_hash, states, writes, poll = _parse(path)
    if len(states) < 3:
        raise Sa1SliceError("slice is too short to establish a transition")
    before, after, stop = states[0], states[1], states[-1]
    if (before[2], before[3]) != (0x008C36, 0x5B):
        raise Sa1SliceError("slice does not begin at SA-1 TCD $00:8C36")
    if (after[2], stop[2], stop[3]) != (0x008C37, 0x008C60, 0x5C):
        raise Sa1SliceError("slice does not reach the expected next frontier")
    if after[7] != before[4]:
        raise Sa1SliceError("TCD did not copy A into D")
    if after[4:7] != before[4:7] or after[8:10] != before[8:10] or after[11] != before[11]:
        raise Sa1SliceError("TCD changed a preserved architectural field")
    expected_nz = (0x80 if before[4] & 0x8000 else 0) | (0x02 if before[4] == 0 else 0)
    if after[10] & 0x82 != expected_nz or after[10] & 0x7d != before[10] & 0x7d:
        raise Sa1SliceError("TCD status transition is inconsistent")
    tcd_writes = [write for write in writes if write[1] == 1]
    if tcd_writes:
        raise Sa1SliceError("TCD unexpectedly emitted a bus write")
    tcd_cycles = after[1] - before[1]
    if tcd_cycles != 2:
        raise Sa1SliceError("TCD reference does not match two architectural cycles")
    if poll[0] != next(state[1] for state in states if state[2] == 0x008C5B):
        raise Sa1SliceError("poll start does not match the first BPL boundary")
    if poll[1] != next(state[1] for state in states if state[2] == 0x008C5D):
        raise Sa1SliceError("poll end does not match the STZ exit boundary")
    iram_writes = sum(0x003000 <= write[3] <= 0x0037ff for write in writes)
    if iram_writes != len(writes):
        raise Sa1SliceError("slice contains a write outside the observed SA-1 IRAM window")

    return {
        "schema_version": 1,
        "source_format": "kss-sa1-tcd-slice-v1",
        "source_sha256": source_hash,
        "processor": "sa1",
        "start_identity": {"pc": before[2], "opcode": before[3], "mode": "e0m0x0"},
        "stop_identity": {
            "reason": "next_unsupported",
            "pc": stop[2],
            "opcode": stop[3],
            "mode": "e0m0x0",
        },
        "tcd_transition": {
            "next_pc": after[2],
            "base_cycles": 2,
            "observed_wait_cycles": 0,
            "observed_total_cycles": tcd_cycles,
            "writes": 0,
            "effects": ["direct_page_from_accumulator", "negative_from_bit15", "zero_from_16bit_zero"],
            "preserved": [
                "accumulator", "index_x", "index_y", "stack_pointer", "data_bank",
                "carry", "irq_disable", "decimal", "index_width", "accumulator_width",
                "overflow", "emulation",
            ],
            "observed_changed_fields": ["direct_page"],
            "before_state_sha256": _digest(before[2:]),
            "after_state_sha256": _digest(after[2:]),
        },
        "slice": {
            "state_records": len(states),
            "write_records": len(writes),
            "write_regions": {"sa1_iram": iram_writes},
            "cycle_start": before[1],
            "cycle_stop": stop[1],
            "cycle_delta": stop[1] - before[1],
            "state_chain_sha256": _digest([state[1:] for state in states]),
            "write_chain_sha256": _digest(writes),
        },
        "polling_boundary": {
            "entry_pc": 0x008C5B,
            "exit_pc": 0x008C5D,
            "cycle_start": poll[0],
            "cycle_end": poll[1],
            "cycle_delta": poll[2],
            "interior_states_compacted": True,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_sa1_slice(args.input), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing SA-1 slice summary: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
