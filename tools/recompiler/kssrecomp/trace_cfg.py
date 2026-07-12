"""Build a deterministic dual-CPU CFG from sanitized trace coverage only.

This module deliberately has no ROM reader.  Its input is the public,
ROM-free coverage format emitted by :mod:`kss_trace.importer`; instruction
bytes and inferred semantics are added by later lifting stages.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any, Mapping

from .cfg import BasicBlock, ControlFlowGraph, Edge, EdgeKind
from .model import BlockIdentity, CpuMode, Processor


class TraceCfgError(ValueError):
    """Sanitized coverage is inconsistent or unsafe to seed."""


@dataclass(frozen=True)
class Observation:
    identity: BlockIdentity
    hits: int
    first_cycle: int
    last_cycle: int


def _identity(value: Mapping[str, Any], context: str) -> BlockIdentity:
    try:
        mode_value = value["mode"]
        if not isinstance(mode_value, Mapping):
            raise TypeError("mode is not an object")
        mode = CpuMode(
            emulation=mode_value["emulation"],
            m8=mode_value["m8"],
            x8=mode_value["x8"],
        )
        processor = Processor(value["processor"])
        pc = value["pc"]
        if isinstance(pc, bool) or not isinstance(pc, int):
            raise TypeError("PC is not an integer")
        return BlockIdentity(processor, pc, mode)
    except (KeyError, TypeError, ValueError) as error:
        raise TraceCfgError(f"{context}: invalid block identity: {error}") from error


def _positive_int(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise TraceCfgError(f"{context} must be a positive integer")
    return value


def _nonnegative_int(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise TraceCfgError(f"{context} must be a non-negative integer")
    return value


def build_trace_seeded_cfg(coverage: Mapping[str, Any]) -> dict[str, object]:
    """Return a serializable CFG seed after strict consistency checks."""
    if coverage.get("schema_version") != 1:
        raise TraceCfgError("unsupported coverage schema_version")
    if coverage.get("source_format") != "mesen-ce-kss-trace-v1":
        raise TraceCfgError("input is not sanitized MesenCE KSS coverage")

    capture = coverage.get("capture")
    if not isinstance(capture, Mapping) or capture.get("bounded") is not True:
        raise TraceCfgError("coverage must describe a bounded capture")
    limit = _positive_int(capture.get("limit"), "capture.limit")
    event_count = _positive_int(capture.get("event_count"), "capture.event_count")
    if event_count > limit:
        raise TraceCfgError("capture.event_count exceeds capture.limit")

    raw_blocks = coverage.get("blocks")
    raw_edges = coverage.get("edges")
    if not isinstance(raw_blocks, list) or not isinstance(raw_edges, list):
        raise TraceCfgError("coverage blocks and edges must be arrays")

    observations: dict[BlockIdentity, Observation] = {}
    for index, raw in enumerate(raw_blocks):
        if not isinstance(raw, Mapping):
            raise TraceCfgError(f"blocks[{index}] is not an object")
        identity = _identity(raw, f"blocks[{index}]")
        if identity in observations:
            raise TraceCfgError(f"duplicate block observation: {identity.symbol}")
        first = _nonnegative_int(raw.get("first_cycle"), f"blocks[{index}].first_cycle")
        last = _nonnegative_int(raw.get("last_cycle"), f"blocks[{index}].last_cycle")
        if last < first:
            raise TraceCfgError(f"blocks[{index}] last_cycle precedes first_cycle")
        observations[identity] = Observation(
            identity, _positive_int(raw.get("hits"), f"blocks[{index}].hits"), first, last
        )

    if not observations:
        raise TraceCfgError("coverage contains no observed blocks")

    graph = ControlFlowGraph()
    for identity in sorted(observations):
        graph.add(BasicBlock(identity))

    serialized_edges: list[dict[str, object]] = []
    seen_edges: set[tuple[BlockIdentity, BlockIdentity]] = set()
    for index, raw in enumerate(raw_edges):
        if not isinstance(raw, Mapping):
            raise TraceCfgError(f"edges[{index}] is not an object")
        source = _identity(raw.get("source", {}), f"edges[{index}].source")
        target = _identity(raw.get("target", {}), f"edges[{index}].target")
        if source not in observations or target not in observations:
            raise TraceCfgError(f"edges[{index}] references an unobserved block")
        if source.processor != target.processor:
            raise TraceCfgError(f"edges[{index}] crosses processor timelines")
        pair = (source, target)
        if pair in seen_edges:
            raise TraceCfgError(f"duplicate observed edge: {source.symbol} -> {target.symbol}")
        seen_edges.add(pair)
        hits = _positive_int(raw.get("hits"), f"edges[{index}].hits")
        evidence_id = f"trace:{source.symbol}->{target.symbol}:hits={hits}"
        graph.blocks[source].edges.append(
            Edge(EdgeKind.OBSERVED, source, target, evidence_id)
        )
        serialized_edges.append({"source": source.to_dict(), "target": target.to_dict(), "hits": hits})

    processors = coverage.get("processors")
    if not isinstance(processors, Mapping):
        raise TraceCfgError("coverage.processors must be an object")
    summaries: dict[str, dict[str, int]] = {}
    entries = []
    for processor in Processor:
        records = [item for item in observations.values() if item.identity.processor == processor]
        raw_summary = processors.get(processor.value)
        if not isinstance(raw_summary, Mapping):
            raise TraceCfgError(f"missing processor summary for {processor.value}")
        events = _nonnegative_int(raw_summary.get("events"), f"processors.{processor.value}.events")
        unique = _nonnegative_int(
            raw_summary.get("unique_blocks"), f"processors.{processor.value}.unique_blocks"
        )
        if unique != len(records) or events != sum(item.hits for item in records):
            raise TraceCfgError(f"processor summary disagrees with blocks for {processor.value}")
        summaries[processor.value] = {"events": events, "unique_blocks": unique}
        if records:
            earliest = min(records, key=lambda item: (item.first_cycle, item.identity.pc, item.identity.mode.key))
            entries.append({
                "kind": "reset_trace_seed",
                "identity": earliest.identity.to_dict(),
                "first_cycle": earliest.first_cycle,
            })

    block_records = [
        {
            "identity": item.identity.to_dict(),
            "hits": item.hits,
            "first_cycle": item.first_cycle,
            "last_cycle": item.last_cycle,
        }
        for item in sorted(observations.values(), key=lambda item: item.identity)
    ]
    serialized_edges.sort(key=lambda item: (
        item["source"]["processor"], item["source"]["pc"],
        _mode_key(item["source"]["mode"]), item["target"]["pc"],
        _mode_key(item["target"]["mode"]),
    ))
    return {
        "schema_version": 1,
        "source": {
            "format": coverage["source_format"],
            "capture": {
                "limit": limit,
                "event_count": event_count,
                "end_reason": capture.get("end_reason"),
                "bounded": True,
            },
        },
        "processors": summaries,
        "entries": entries,
        "observations": {"blocks": block_records, "edges": serialized_edges},
        "cfg": graph.to_dict(),
    }


def _mode_key(mode: Mapping[str, Any]) -> str:
    return f"e{int(mode['emulation'])}m{int(mode['m8'])}x{int(mode['x8'])}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="sanitized coverage JSON")
    parser.add_argument("output", type=Path, help="ROM-free trace CFG JSON")
    parser.add_argument("--check", action="store_true", help="fail if output is not current")
    args = parser.parse_args(argv)
    try:
        value = json.loads(args.input.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        parser.error(str(error))
    result = build_trace_seeded_cfg(value)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing trace CFG: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
