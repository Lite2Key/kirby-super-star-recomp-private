"""Build a ROM-free execution-frontier inventory from lifted trace artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping

from .codegen import LIFTED_V1_OPCODES


class FrontierError(ValueError):
    pass


def _key(identity: Mapping[str, Any]) -> tuple[Any, ...]:
    mode = identity["mode"]
    return identity["processor"], identity["pc"], mode["emulation"], mode["m8"], mode["x8"]


def build_frontier(
    coverage: Mapping[str, Any], lifted: Mapping[str, Any], baseline: Mapping[str, Any]
) -> dict[str, Any]:
    if any(item.get("schema_version") != 1 for item in (coverage, lifted, baseline)):
        raise FrontierError("all inputs must use schema version 1")
    if coverage.get("capture", {}).get("end_reason") != "complete":
        raise FrontierError("frontier coverage must end at an observed boundary")
    observed = {_key(block): block for block in coverage.get("blocks", [])}
    old = {_key(block["identity"]) for block in baseline.get("blocks", [])}
    blocks = lifted.get("blocks", [])
    records: dict[str, list[dict[str, Any]]] = {"scpu": [], "sa1": []}
    unresolved: dict[str, list[dict[str, Any]]] = {"scpu": [], "sa1": []}
    for block in blocks:
        identity = block["identity"]
        processor = identity["processor"]
        if block["status"] != "decoded" or block["instruction"] is None:
            unresolved[processor].append({"identity": identity, "reason": block["unresolved_reason"]})
            continue
        instruction = block["instruction"]
        if instruction["opcode"] not in LIFTED_V1_OPCODES:
            observation = observed[_key(identity)]
            records[processor].append({
                "identity": identity,
                "opcode": instruction["opcode"],
                "mnemonic": instruction["mnemonic"],
                "first_cycle": observation["first_cycle"],
                "hits": observation["hits"],
            })

    processors: dict[str, Any] = {}
    for processor in ("scpu", "sa1"):
        processor_blocks = [b for b in blocks if b["identity"]["processor"] == processor]
        missing = sorted(records[processor], key=lambda item: (item["first_cycle"], item["identity"]["pc"]))
        opcode_counts: dict[tuple[int, str], int] = {}
        for item in missing:
            pair = item["opcode"], item["mnemonic"]
            opcode_counts[pair] = opcode_counts.get(pair, 0) + 1
        processors[processor] = {
            "reachable_nodes": len(processor_blocks),
            "new_nodes": sum(_key(b["identity"]) not in old for b in processor_blocks),
            "unresolved_nodes": len(unresolved[processor]),
            "first_unsupported": missing[0] if missing else None,
            "unsupported_opcodes": [
                {"opcode": opcode, "mnemonic": mnemonic, "nodes": count}
                for (opcode, mnemonic), count in sorted(opcode_counts.items())
            ],
        }
    return {
        "schema_version": 1,
        "boundary": "first-snes-end-frame",
        "capture": {
            "event_count": coverage["capture"]["event_count"],
            "scpu_events": coverage["processors"]["scpu"]["events"],
            "sa1_events": coverage["processors"]["sa1"]["events"],
        },
        "generated_nodes": sum(len([b for b in blocks if b["status"] == "decoded"]) for _ in [0]),
        "processors": processors,
        "unresolved": unresolved,
        "failure_policy": "generated-dispatch-stops-on-first-unsupported-or-unobserved-identity",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("coverage", type=Path)
    parser.add_argument("lifted", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = build_frontier(*(
            json.loads(path.read_text(encoding="utf-8"))
            for path in (args.coverage, args.lifted, args.baseline)
        ))
    except (OSError, json.JSONDecodeError, FrontierError) as error:
        parser.error(str(error))
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing frontier inventory: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
