"""Sanitize MesenCE's compact reset-to-first-visible route aggregation."""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import json
from pathlib import Path
import re
from typing import Iterable

from .importer import BlockKey, TraceFormatError


CONTRACT = "reset-to-first-visible-v1"
START = re.compile(r"KSS_COMPACT_ROUTE_START_V1\|(\d+)\|(\d+)\|(\d+)\|(\d+)")
BLOCK = re.compile(
    r"KSS_COMPACT_ROUTE_BLOCK_V1\|(scpu|sa1)\|([0-9A-Fa-f]{6})\|"
    r"([01])\|([01])\|([01])\|(\d+)\|(\d+)\|(\d+)"
)
EDGE = re.compile(
    r"KSS_COMPACT_ROUTE_EDGE_V1\|(scpu|sa1)\|([0-9A-Fa-f]{6})\|"
    r"([01])\|([01])\|([01])\|(scpu|sa1)\|([0-9A-Fa-f]{6})\|"
    r"([01])\|([01])\|([01])\|(\d+)"
)
END = re.compile(
    r"KSS_COMPACT_ROUTE_END_V1\|(\d+)\|visible\|(\d+)\|(\d+)\|(\d+)\|"
    r"(\d+)\|(\d+)\|(\d+)\|(\d+)"
)
ABORT = re.compile(r"KSS_COMPACT_ROUTE_ABORT_V1\|([a-z_]+)")


@dataclass(frozen=True)
class BlockRecord:
    identity: BlockKey
    hits: int
    first_cycle: int
    last_cycle: int


def _identity(parts: tuple[str, ...], context: str) -> BlockKey:
    processor, pc, emulation, m8, x8 = parts
    result = BlockKey(
        processor=processor,
        pc=int(pc, 16),
        emulation=emulation == "1",
        m8=m8 == "1",
        x8=x8 == "1",
    )
    if result.emulation and not (result.m8 and result.x8):
        raise TraceFormatError(f"{context}: emulation mode requires M=1 and X=1")
    return result


def import_compact_route_lines(lines: Iterable[str]) -> dict[str, object]:
    """Validate aggregate markers without expanding them into instruction events."""
    limits: tuple[int, int, int, int] | None = None
    blocks: dict[BlockKey, BlockRecord] = {}
    edges: dict[tuple[BlockKey, BlockKey], int] = {}
    end: tuple[int, ...] | None = None

    for line in lines:
        if match := ABORT.search(line):
            raise TraceFormatError(f"compact route capture aborted: {match.group(1)}")
        if match := START.search(line):
            if limits is not None:
                raise TraceFormatError("multiple compact route start markers")
            limits = tuple(map(int, match.groups()))
            continue
        if match := BLOCK.search(line):
            if limits is None or end is not None:
                raise TraceFormatError("compact route block lies outside capture markers")
            groups = match.groups()
            identity = _identity(groups[:5], "block")
            hits, first_cycle, last_cycle = map(int, groups[5:])
            if identity in blocks:
                raise TraceFormatError("duplicate compact route block identity")
            if hits < 1 or first_cycle > last_cycle:
                raise TraceFormatError("invalid compact route block counters")
            blocks[identity] = BlockRecord(identity, hits, first_cycle, last_cycle)
            continue
        if match := EDGE.search(line):
            if limits is None or end is not None:
                raise TraceFormatError("compact route edge lies outside capture markers")
            groups = match.groups()
            source = _identity(groups[:5], "edge source")
            target = _identity(groups[5:10], "edge target")
            hits = int(groups[10])
            pair = (source, target)
            if pair in edges:
                raise TraceFormatError("duplicate compact route edge identity")
            if source.processor != target.processor:
                raise TraceFormatError("compact route edge crosses processor timelines")
            if hits < 1:
                raise TraceFormatError("compact route edge has no hits")
            edges[pair] = hits
            continue
        if match := END.search(line):
            if end is not None:
                raise TraceFormatError("multiple compact route end markers")
            end = tuple(map(int, match.groups()))

    if limits is None:
        raise TraceFormatError("missing compact route start marker")
    event_limit, frame_limit, block_limit, edge_limit = limits
    if not 1 <= event_limit <= 5_000_000:
        raise TraceFormatError("event limit is outside the sanitizer safety range")
    if not 1 <= frame_limit <= 10_000:
        raise TraceFormatError("frame limit is outside the sanitizer safety range")
    if not 1 <= block_limit <= event_limit or not 1 <= edge_limit <= event_limit:
        raise TraceFormatError("aggregate limits are inconsistent with the event limit")
    if end is None:
        raise TraceFormatError("missing compact route visible completion marker")
    if not blocks:
        raise TraceFormatError("compact route contains no blocks")

    event_count, frames, width, height, pixel_count, nonblack, block_count, edge_count = end
    if not 1 <= event_count <= event_limit:
        raise TraceFormatError("compact route event count exceeds its limit")
    if not 1 <= frames <= frame_limit:
        raise TraceFormatError("visible frame lies outside the declared frame window")
    if width < 1 or height < 1 or pixel_count != width * height:
        raise TraceFormatError("visible-frame dimensions and pixel count disagree")
    if not 1 <= nonblack <= pixel_count:
        raise TraceFormatError("visible-frame marker does not prove a nonblack frame")
    if block_count != len(blocks) or block_count > block_limit:
        raise TraceFormatError("compact route block count disagrees with aggregate markers")
    if edge_count != len(edges) or edge_count > edge_limit:
        raise TraceFormatError("compact route edge count disagrees with aggregate markers")
    if sum(item.hits for item in blocks.values()) != event_count:
        raise TraceFormatError("block hit sum does not match compact route event count")

    processor_events: dict[str, int] = defaultdict(int)
    processor_edge_hits: dict[str, int] = defaultdict(int)
    for item in blocks.values():
        processor_events[item.identity.processor] += item.hits
    for (source, target), hits in edges.items():
        if source not in blocks or target not in blocks:
            raise TraceFormatError("compact route edge references an unobserved block")
        processor_edge_hits[source.processor] += hits
    for processor in ("scpu", "sa1"):
        events = processor_events[processor]
        if events < 1:
            raise TraceFormatError(f"compact route is missing {processor} execution")
        if processor_edge_hits[processor] != events - 1:
            raise TraceFormatError(f"{processor} edge hit sum does not form one continuous route")

    serialized_blocks = [
        {
            **item.identity.to_dict(),
            "hits": item.hits,
            "first_cycle": item.first_cycle,
            "last_cycle": item.last_cycle,
        }
        for item in sorted(blocks.values(), key=lambda item: item.identity)
    ]
    serialized_edges = [
        {"source": source.to_dict(), "target": target.to_dict(), "hits": hits}
        for (source, target), hits in sorted(edges.items())
    ]
    return {
        "schema_version": 1,
        "source_format": "mesen-ce-kss-trace-v1",
        "capture": {
            "limit": event_limit,
            "event_count": event_count,
            "end_reason": "complete",
            "bounded": True,
            "route_capture": {
                "contract": CONTRACT,
                "aggregation": "in_emulator",
                "start_event": "reset",
                "stop_event": "first_visible_end_frame",
                "frames_elapsed": frames,
                "unique_blocks": block_count,
                "unique_edges": edge_count,
                "visible_frame": {
                    "width": width,
                    "height": height,
                    "pixel_count": pixel_count,
                    "nonblack_pixels": nonblack,
                },
            },
        },
        "processors": {
            processor: {
                "events": processor_events[processor],
                "unique_blocks": sum(
                    identity.processor == processor for identity in blocks
                ),
            }
            for processor in ("scpu", "sa1")
        },
        "blocks": serialized_blocks,
        "edges": serialized_edges,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="private raw MesenCE log")
    parser.add_argument("output", type=Path, help="ROM-free aggregate coverage JSON")
    args = parser.parse_args(argv)
    try:
        result = import_compact_route_lines(
            args.input.read_text(encoding="utf-8", errors="replace").splitlines()
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, TraceFormatError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
