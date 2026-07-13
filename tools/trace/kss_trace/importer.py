"""Convert private trace text into deterministic, ROM-free coverage metadata."""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import json
from pathlib import Path
import re
from typing import Iterable


EVENT = re.compile(
    r"KSS_TRACE_V1\|(\d+)\|(scpu|sa1)\|(\d+)\|([0-9A-Fa-f]{6})\|([01])\|([01])\|([01])"
)
START = re.compile(r"KSS_TRACE_START_V1\|(\d+)")
END = re.compile(r"KSS_TRACE_END_V1\|(\d+)\|(limit|complete)")


class TraceFormatError(ValueError):
    """A trace cannot be sanitized without guessing."""


@dataclass(frozen=True, order=True)
class BlockKey:
    processor: str
    pc: int
    emulation: bool
    m8: bool
    x8: bool

    def to_dict(self) -> dict[str, object]:
        return {
            "processor": self.processor,
            "pc": self.pc,
            "mode": {"emulation": self.emulation, "m8": self.m8, "x8": self.x8},
        }


@dataclass(frozen=True)
class Event:
    sequence: int
    block: BlockKey
    cycle: int


def _parse_event(match: re.Match[str]) -> Event:
    sequence, processor, cycle, pc, emulation, m8, x8 = match.groups()
    block = BlockKey(
        processor=processor,
        pc=int(pc, 16),
        emulation=emulation == "1",
        m8=m8 == "1",
        x8=x8 == "1",
    )
    if block.emulation and not (block.m8 and block.x8):
        raise TraceFormatError(f"event {sequence}: emulation mode requires M=1 and X=1")
    return Event(int(sequence), block, int(cycle))


def summarize_events(
    events: list[Event], capture_limit: int, end_reason: str,
    *, capture_extra: dict[str, object] | None = None,
) -> dict[str, object]:
    """Reduce validated identity-only events to the shared ROM-free format."""
    if not 1 <= capture_limit <= 1_000_000:
        raise TraceFormatError("capture limit is outside the sanitizer safety range")
    if not events:
        raise TraceFormatError("trace contains no execution events")
    expected = 1
    previous_cycle: dict[str, int] = {}
    for event in events:
        if event.sequence != expected:
            raise TraceFormatError(
                f"non-contiguous sequence: expected {expected}, observed {event.sequence}"
            )
        expected += 1
        prior = previous_cycle.get(event.block.processor)
        if prior is not None and event.cycle < prior:
            raise TraceFormatError(f"{event.block.processor} cycle count moved backwards")
        previous_cycle[event.block.processor] = event.cycle

    if len(events) > capture_limit:
        raise TraceFormatError("event count exceeds declared capture limit")
    stats: dict[BlockKey, list[int]] = {}
    edges: dict[tuple[BlockKey, BlockKey], int] = defaultdict(int)
    previous_block: dict[str, BlockKey] = {}
    processor_events: dict[str, int] = defaultdict(int)
    for event in events:
        processor_events[event.block.processor] += 1
        if event.block not in stats:
            stats[event.block] = [0, event.cycle, event.cycle]
        record = stats[event.block]
        record[0] += 1
        record[2] = event.cycle
        if event.block.processor in previous_block:
            edges[(previous_block[event.block.processor], event.block)] += 1
        previous_block[event.block.processor] = event.block

    blocks = []
    for block in sorted(stats):
        hits, first_cycle, last_cycle = stats[block]
        blocks.append({
            **block.to_dict(),
            "hits": hits,
            "first_cycle": first_cycle,
            "last_cycle": last_cycle,
        })

    serialized_edges = []
    for (source, target), hits in sorted(edges.items()):
        serialized_edges.append({"source": source.to_dict(), "target": target.to_dict(), "hits": hits})

    capture: dict[str, object] = {
        "limit": capture_limit,
        "event_count": len(events),
        "end_reason": end_reason,
        "bounded": len(events) <= capture_limit,
    }
    if capture_extra:
        capture.update(capture_extra)
    return {
        "schema_version": 1,
        "source_format": "mesen-ce-kss-trace-v1",
        "capture": capture,
        "processors": {
            name: {"events": processor_events.get(name, 0),
                   "unique_blocks": sum(block.processor == name for block in stats)}
            for name in ("scpu", "sa1")
        },
        "blocks": blocks,
        "edges": serialized_edges,
    }


def import_lines(lines: Iterable[str]) -> dict[str, object]:
    """Sanitize Mesen log lines; unrelated emulator diagnostics are ignored."""
    events: list[Event] = []
    capture_limit: int | None = None
    end_count: int | None = None
    end_reason: str | None = None

    for line in lines:
        if match := START.search(line):
            if capture_limit is not None:
                raise TraceFormatError("multiple trace start markers")
            capture_limit = int(match.group(1))
        if match := EVENT.search(line):
            events.append(_parse_event(match))
        if match := END.search(line):
            if end_count is not None:
                raise TraceFormatError("multiple trace end markers")
            end_count = int(match.group(1))
            end_reason = match.group(2)

    if capture_limit is None:
        raise TraceFormatError("missing trace start marker")
    if end_count is not None and end_count != len(events):
        raise TraceFormatError("end marker count does not match execution events")
    if end_reason == "limit" and len(events) != capture_limit:
        raise TraceFormatError("limit end marker was emitted before the declared limit")
    return summarize_events(
        events, capture_limit, end_reason or "missing_end_marker"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="private raw MesenCE log")
    parser.add_argument("output", type=Path, help="ROM-free coverage JSON")
    args = parser.parse_args(argv)
    result = import_lines(args.input.read_text(encoding="utf-8", errors="replace").splitlines())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
