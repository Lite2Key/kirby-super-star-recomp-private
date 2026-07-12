"""Sanitize a private first-frame Mesen oracle into value-free digests."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
from typing import Any


class FrameOracleError(ValueError):
    """The private oracle is incomplete, malformed, or internally inconsistent."""


@dataclass(frozen=True)
class FrameOracle:
    source_sha256: str
    master_clock: int
    processor_cycles: dict[str, int]
    states: dict[str, tuple[int, ...]]
    writes: tuple[tuple[int, str, int, int, int], ...]


def _integer(text: str, label: str, *, maximum: int | None = None) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise FrameOracleError(f"invalid {label}") from exc
    if value < 0 or (maximum is not None and value > maximum):
        raise FrameOracleError(f"{label} is outside its allowed range")
    return value


def _hex_address(text: str) -> int:
    if len(text) != 6 or any(char not in "0123456789abcdefABCDEF" for char in text):
        raise FrameOracleError("invalid 24-bit address")
    return int(text, 16)


def _digest(value: object) -> str:
    encoded = json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def parse_frame_oracle(path: Path) -> FrameOracle:
    raw = path.read_bytes()
    try:
        lines = raw.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise FrameOracleError("oracle log is not UTF-8") from exc

    records = [line for line in lines if line.startswith("KSS_FRAME_")]
    starts = [line for line in records if line.startswith("KSS_FRAME_START_V1|")]
    ends = [line for line in records if line.startswith("KSS_FRAME_END_V1|")]
    aborts = [line for line in records if line.startswith("KSS_FRAME_ABORT_V1|")]
    if aborts:
        raise FrameOracleError("oracle capture aborted")
    if len(starts) != 1 or len(ends) != 1:
        raise FrameOracleError("oracle requires exactly one start and end marker")
    if records[0] != starts[0] or records[-1] != ends[0]:
        raise FrameOracleError("oracle markers do not bound every private record")

    start = starts[0].split("|")
    if len(start) != 2:
        raise FrameOracleError("malformed start marker")
    limit = _integer(start[1], "write limit")
    if not 1 <= limit <= 2_000_000:
        raise FrameOracleError("write limit is outside the sanitizer safety range")

    writes: list[tuple[int, str, int, int, int]] = []
    states: dict[str, tuple[int, ...]] = {}
    for line in records[1:-1]:
        parts = line.split("|")
        if parts[0] == "KSS_FRAME_WRITE_V1":
            if len(parts) != 6:
                raise FrameOracleError("malformed write record")
            ordinal = _integer(parts[1], "write ordinal")
            processor = parts[2]
            if processor not in {"scpu", "sa1"}:
                raise FrameOracleError("invalid write processor")
            cycle = _integer(parts[3], "write cycle")
            address = _hex_address(parts[4])
            value = _integer(parts[5], "write value", maximum=0xff)
            if ordinal != len(writes) + 1:
                raise FrameOracleError("write ordinals are not contiguous")
            writes.append((ordinal, processor, cycle, address, value))
        elif parts[0] == "KSS_FRAME_STATE_V1":
            if len(parts) != 12:
                raise FrameOracleError("malformed final-state record")
            processor = parts[1]
            if processor not in {"scpu", "sa1"} or processor in states:
                raise FrameOracleError("duplicate or invalid final-state processor")
            cycle = _integer(parts[2], "state cycle")
            pc = _hex_address(parts[3])
            a = _integer(parts[4], "A", maximum=0xffff)
            x = _integer(parts[5], "X", maximum=0xffff)
            y = _integer(parts[6], "Y", maximum=0xffff)
            direct = _integer(parts[7], "D", maximum=0xffff)
            stack = _integer(parts[8], "SP", maximum=0xffff)
            dbr = _integer(parts[9], "DBR", maximum=0xff)
            status = _integer(parts[10], "P", maximum=0xff)
            emulation = _integer(parts[11], "E", maximum=1)
            states[processor] = (cycle, pc, a, x, y, direct, stack, dbr, status, emulation)
        else:
            raise FrameOracleError("unknown private oracle record")

    end = ends[0].split("|")
    if len(end) != 6 or end[1] != "1":
        raise FrameOracleError("malformed first-frame end marker")
    master = _integer(end[2], "master clock")
    processor_cycles = {
        "scpu": _integer(end[3], "S-CPU cycle"),
        "sa1": _integer(end[4], "SA-1 cycle"),
    }
    write_count = _integer(end[5], "write count")
    if write_count != len(writes) or write_count > limit:
        raise FrameOracleError("end-marker write count disagrees with records")
    if set(states) != {"scpu", "sa1"}:
        raise FrameOracleError("oracle must contain both final CPU states")
    for processor, cycle in processor_cycles.items():
        if states[processor][0] != cycle:
            raise FrameOracleError(f"{processor} final-state cycle disagrees with end marker")
        if any(write[1] == processor and write[2] > cycle for write in writes):
            raise FrameOracleError(f"{processor} write occurs after its final cycle")

    return FrameOracle(
        hashlib.sha256(raw).hexdigest(), master, processor_cycles,
        states, tuple(writes),
    )


def _is_hardware_bank(address: int) -> bool:
    return ((address >> 16) & 0x40) == 0


def _event_kind(address: int) -> str | None:
    if not _is_hardware_bank(address):
        return None
    offset = address & 0xffff
    if 0x2100 <= offset <= 0x213f:
        return "ppu_register"
    if offset in {0x420b, 0x420c} or 0x4300 <= offset <= 0x437f:
        return "dma_register"
    return None


def summarize_frame_oracle(path: Path) -> dict[str, Any]:
    oracle = parse_frame_oracle(path)
    states = []
    processors = []
    for processor in ("scpu", "sa1"):
        chain = [write for write in oracle.writes if write[1] == processor]
        states.append({
            "processor": processor,
            "cycle": oracle.processor_cycles[processor],
            "architectural_state_sha256": _digest(oracle.states[processor][1:]),
        })
        processors.append({
            "processor": processor,
            "records": len(chain),
            "chain_sha256": _digest(chain),
        })
    events = []
    for kind in ("ppu_register", "dma_register"):
        chain = [write for write in oracle.writes if _event_kind(write[3]) == kind]
        events.append({"kind": kind, "records": len(chain), "chain_sha256": _digest(chain)})
    return {
        "schema_version": 1,
        "source_format": "kss-first-frame-v1",
        "source_sha256": oracle.source_sha256,
        "termination": "first_end_frame",
        "frame_number": 1,
        "clocks": {
            "master": oracle.master_clock,
            "processors": oracle.processor_cycles,
        },
        "final_states": states,
        "writes": {
            "total_records": len(oracle.writes),
            "chain_sha256": _digest(oracle.writes),
            "processors": processors,
        },
        "register_events": events,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_frame_oracle(args.input), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing first-frame summary: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
