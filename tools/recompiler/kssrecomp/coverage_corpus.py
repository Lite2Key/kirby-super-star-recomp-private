"""Merge bounded sanitized route captures into one conservative code corpus."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping, Sequence

from .trace_cfg import TraceCfgError, build_trace_seeded_cfg


class CoverageCorpusError(ValueError):
    """Route captures cannot be safely combined."""


def _key(value: Mapping[str, Any]) -> tuple[Any, ...]:
    mode = value["mode"]
    return (
        value["processor"], value["pc"],
        mode["emulation"], mode["m8"], mode["x8"],
    )


def _entry_keys(coverage: Mapping[str, Any]) -> dict[str, tuple[Any, ...]]:
    try:
        graph = build_trace_seeded_cfg(coverage)
    except TraceCfgError as error:
        raise CoverageCorpusError(str(error)) from error
    return {
        item["identity"]["processor"]: _key(item["identity"])
        for item in graph["entries"]
    }


def merge_coverages(
    captures: Sequence[tuple[str, Mapping[str, Any]]],
) -> dict[str, Any]:
    if not captures:
        raise CoverageCorpusError("at least one route capture is required")
    route_ids = [route_id for route_id, _ in captures]
    if any(not route_id or not route_id.replace("-", "").replace("_", "").isalnum()
           for route_id in route_ids):
        raise CoverageCorpusError("route ids must be non-empty identifier fragments")
    if len(set(route_ids)) != len(route_ids):
        raise CoverageCorpusError("duplicate route id")

    expected_entries: dict[str, tuple[Any, ...]] | None = None
    blocks: dict[tuple[Any, ...], dict[str, Any]] = {}
    edges: dict[tuple[tuple[Any, ...], tuple[Any, ...]], dict[str, Any]] = {}
    total_events = 0
    total_limit = 0
    for route_id, coverage in captures:
        entries = _entry_keys(coverage)
        if expected_entries is None:
            expected_entries = entries
        elif entries != expected_entries:
            raise CoverageCorpusError(
                f"route {route_id} does not share the corpus reset entries"
            )
        capture = coverage["capture"]
        total_events += capture["event_count"]
        total_limit += capture["limit"]
        for item in coverage["blocks"]:
            key = _key(item)
            prior = blocks.get(key)
            if prior is None:
                prior = {
                    **item,
                    "hits": 0,
                    "first_cycle": item["first_cycle"],
                    "last_cycle": item["last_cycle"],
                    "routes": [],
                }
                blocks[key] = prior
            prior["hits"] += item["hits"]
            prior["first_cycle"] = min(prior["first_cycle"], item["first_cycle"])
            prior["last_cycle"] = max(prior["last_cycle"], item["last_cycle"])
            prior["routes"].append(route_id)
        for item in coverage["edges"]:
            pair = (_key(item["source"]), _key(item["target"]))
            prior = edges.get(pair)
            if prior is None:
                prior = {**item, "hits": 0, "routes": []}
                edges[pair] = prior
            prior["hits"] += item["hits"]
            prior["routes"].append(route_id)

    ordered_blocks = sorted(blocks.values(), key=lambda item: _key(item))
    ordered_edges = sorted(edges.values(), key=lambda item: (_key(item["source"]), _key(item["target"])))
    processors = {}
    for processor in ("scpu", "sa1"):
        records = [item for item in ordered_blocks if item["processor"] == processor]
        processors[processor] = {
            "events": sum(item["hits"] for item in records),
            "unique_blocks": len(records),
        }
    result = {
        "schema_version": 1,
        "source_format": "mesen-ce-kss-trace-v1",
        "capture": {
            "bounded": True,
            "limit": total_limit,
            "event_count": total_events,
            "end_reason": "complete",
            "corpus_policy": "union-of-bounded-reset-rooted-routes",
            "routes": route_ids,
        },
        "processors": processors,
        "blocks": ordered_blocks,
        "edges": ordered_edges,
    }
    # Prove the merged artifact still satisfies the same strict CFG contract.
    build_trace_seeded_cfg(result)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path, help="sanitized coverage JSON files")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        captures = [
            (path.stem, json.loads(path.read_text(encoding="utf-8")))
            for path in args.captures
        ]
        rendered = json.dumps(merge_coverages(captures), indent=2, sort_keys=True) + "\n"
        if args.check:
            if not args.out.exists() or args.out.read_text(encoding="utf-8") != rendered:
                raise CoverageCorpusError(f"stale or missing coverage corpus: {args.out}")
        else:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(rendered, encoding="utf-8")
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
