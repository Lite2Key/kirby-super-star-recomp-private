"""Lift bounded reset paths from vectors, sanitized trace CFG, and a private ROM.

The emitted artifact contains at most one instruction (four bytes maximum) per
observed node. It never exports unreferenced ROM ranges, data, or strings.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any, Mapping

from .decoder import DecoderState, Flow, decode_one
from .errors import AmbiguousModeError, DecodeError
from .model import BlockIdentity, CpuMode, Processor
from .rom import _strip_copier_header


class ResetLiftError(ValueError):
    """The inputs cannot safely establish a reset path."""


def _identity(value: Mapping[str, Any]) -> BlockIdentity:
    try:
        mode = value["mode"]
        return BlockIdentity(
            Processor(value["processor"]),
            value["pc"],
            CpuMode(mode["emulation"], mode["m8"], mode["x8"]),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise ResetLiftError(f"invalid trace CFG identity: {error}") from error


def _rom_offset(address: int, payload_size: int) -> int | None:
    if address & 0xFFFF < 0x8000:
        return None
    return ((((address >> 16) & 0x7F) << 15) | (address & 0x7FFF)) % payload_size


def _edge_is_supported(
    flow: Flow, pc: int, size: int, target: int | None, successor: int, opcode: int | None = None
) -> bool:
    fallthrough = (pc & 0xFF0000) | ((pc + size) & 0xFFFF)
    # MVN/MVP are restartable instructions: one architectural transfer keeps
    # PC at the opcode while A has not wrapped, then exits to the fallthrough.
    if opcode in (0x44, 0x54):
        return successor in (pc, fallthrough)
    if flow == Flow.NEXT:
        return successor == fallthrough
    if flow == Flow.BRANCH:
        return successor in (fallthrough, target)
    if flow in (Flow.CALL, Flow.JUMP):
        return target is not None and successor == target
    if flow == Flow.STOP:
        return False
    # Returns, interrupts, and interrupt returns require runtime stack/vector state.
    return False


def _instruction_dict(instruction: Any) -> dict[str, object]:
    return {
        "pc": instruction.pc,
        "bytes_hex": instruction.bytes_.hex().upper(),
        "opcode": instruction.opcode,
        "mnemonic": instruction.mnemonic,
        "addressing": instruction.addressing.value,
        "size": instruction.size,
        "flow": instruction.flow.value,
        "target": instruction.target,
        "mode_after": {
            "emulation": instruction.state_after.mode.emulation,
            "m8": instruction.state_after.mode.m8,
            "x8": instruction.state_after.mode.x8,
        },
    }


@dataclass(frozen=True)
class _State:
    carry: bool | None


def lift_reset_paths(
    vectors: Mapping[str, Any],
    trace_cfg: Mapping[str, Any],
    rom: bytes,
    *,
    max_blocks_per_processor: int = 256,
) -> dict[str, object]:
    if max_blocks_per_processor < 1 or max_blocks_per_processor > 4096:
        raise ResetLiftError("max_blocks_per_processor must be between 1 and 4096")
    if vectors.get("schema_version") != 1 or trace_cfg.get("schema_version") != 1:
        raise ResetLiftError("unsupported input schema version")
    cartridge = vectors.get("cartridge", {})
    if cartridge.get("mapping") != "sa1-lorom":
        raise ResetLiftError("reset lifting currently requires an SA-1 LoROM mapping")
    payload, copier_header = _strip_copier_header(rom)
    if len(payload) != cartridge.get("payload_size") or copier_header != cartridge.get("copier_header"):
        raise ResetLiftError("ROM layout does not match reset-vector metadata")

    cfg = trace_cfg.get("cfg", {})
    raw_blocks = cfg.get("blocks")
    raw_entries = trace_cfg.get("entries")
    if not isinstance(raw_blocks, list) or not isinstance(raw_entries, list):
        raise ResetLiftError("trace CFG is missing blocks or entries")
    identities: set[BlockIdentity] = set()
    outgoing: dict[BlockIdentity, list[BlockIdentity]] = {}
    for raw in raw_blocks:
        identity = _identity(raw["identity"])
        if identity in identities:
            raise ResetLiftError(f"duplicate trace CFG node: {identity.symbol}")
        identities.add(identity)
        outgoing[identity] = [_identity(edge["target"]) for edge in raw.get("edges", [])]
    for source, targets in outgoing.items():
        if any(target not in identities for target in targets):
            raise ResetLiftError(f"trace CFG edge from {source.symbol} has missing target")
        if any(target.processor != source.processor for target in targets):
            raise ResetLiftError(f"trace CFG edge from {source.symbol} crosses processors")

    entries = {(_identity(item["identity"]).processor): _identity(item["identity"]) for item in raw_entries}
    if set(entries) != set(Processor):
        raise ResetLiftError("trace CFG must provide one reset seed for each processor")
    scpu_vector = next(
        (item for item in vectors["processors"]["scpu"]["vectors"] if item["name"] == "emulation_reset"),
        None,
    )
    sa1_vector = vectors["processors"]["sa1"].get("observed_reset_entry")
    if scpu_vector is None or sa1_vector is None:
        raise ResetLiftError("reset-vector metadata lacks S-CPU or observed SA-1 reset entry")
    if entries[Processor.SCPU].pc != scpu_vector["target_cpu_address"]:
        raise ResetLiftError("S-CPU reset vector disagrees with trace seed")
    if entries[Processor.SA1].pc != sa1_vector["target_cpu_address"]:
        raise ResetLiftError("SA-1 observed reset entry disagrees with trace seed")

    emitted_blocks: list[dict[str, object]] = []
    emitted_edges: list[dict[str, object]] = []
    regions: dict[str, dict[str, object]] = {}
    for processor in Processor:
        entry = entries[processor]
        pending = deque([entry])
        states: dict[BlockIdentity, _State] = {entry: _State(None)}
        visited: set[BlockIdentity] = set()
        processor_blocks: list[dict[str, object]] = []
        processor_edges: list[dict[str, object]] = []
        while pending and len(visited) < max_blocks_per_processor:
            identity = pending.popleft()
            if identity in visited:
                continue
            visited.add(identity)
            offset = _rom_offset(identity.pc, len(payload))
            record: dict[str, object] = {
                "identity": identity.to_dict(),
                "rom_offset": offset,
                "status": "decoded",
                "instruction": None,
                "unresolved_reason": None,
            }
            if offset is None:
                record["status"] = "unresolved"
                record["unresolved_reason"] = "address_not_rom_mapped"
                processor_blocks.append(record)
                continue
            state = DecoderState(identity.mode, states[identity].carry)
            try:
                instruction = decode_one(payload[offset : offset + 4], identity.pc, state)
            except AmbiguousModeError as error:
                record["status"] = "unresolved"
                record["unresolved_reason"] = f"ambiguous_mode:{error}"
                processor_blocks.append(record)
                continue
            except DecodeError as error:
                record["status"] = "unresolved"
                record["unresolved_reason"] = f"decode_error:{error}"
                processor_blocks.append(record)
                continue
            record["instruction"] = _instruction_dict(instruction)
            targets = sorted(outgoing[identity], key=lambda item: (item.pc, item.mode.key))
            if instruction.flow in (Flow.RETURN, Flow.INTERRUPT, Flow.INTERRUPT_RETURN) or (
                instruction.flow in (Flow.CALL, Flow.JUMP) and instruction.target is None
            ):
                record["status"] = "unresolved"
                record["unresolved_reason"] = "unsupported_dynamic_control_flow"
                processor_blocks.append(record)
                continue
            if instruction.flow == Flow.STOP and targets:
                record["status"] = "unresolved"
                record["unresolved_reason"] = "observed_control_flow_mismatch"
                processor_blocks.append(record)
                continue
            accepted: list[BlockIdentity] = []
            mismatch = False
            for target in targets:
                if target.mode != instruction.state_after.mode or not _edge_is_supported(
                    instruction.flow, identity.pc, instruction.size, instruction.target, target.pc,
                    instruction.opcode
                ):
                    mismatch = True
                    break
                accepted.append(target)
            if mismatch:
                record["status"] = "unresolved"
                record["unresolved_reason"] = "observed_control_flow_or_mode_mismatch"
                processor_blocks.append(record)
                continue
            processor_blocks.append(record)
            for target in accepted:
                processor_edges.append({"source": identity.to_dict(), "target": target.to_dict()})
                next_state = _State(instruction.state_after.carry)
                prior = states.get(target)
                merged = next_state if prior is None or prior == next_state else _State(None)
                if prior != merged:
                    states[target] = merged
                if target not in visited:
                    pending.append(target)

        processor_blocks.sort(key=lambda item: (item["identity"]["pc"], _mode_key(item["identity"]["mode"])))
        processor_edges.sort(key=lambda item: (
            item["source"]["pc"], _mode_key(item["source"]["mode"]),
            item["target"]["pc"], _mode_key(item["target"]["mode"]),
        ))
        emitted_blocks.extend(processor_blocks)
        emitted_edges.extend(processor_edges)
        regions[processor.value] = {
            "entry": entry.to_dict(),
            "decoded_blocks": sum(block["status"] == "decoded" for block in processor_blocks),
            "unresolved_blocks": sum(block["status"] == "unresolved" for block in processor_blocks),
            "limit_reached": bool(pending),
        }

    emitted_blocks.sort(key=lambda item: (
        item["identity"]["processor"], item["identity"]["pc"], _mode_key(item["identity"]["mode"])
    ))
    emitted_edges.sort(key=lambda item: (
        item["source"]["processor"], item["source"]["pc"], _mode_key(item["source"]["mode"]),
        item["target"]["pc"], _mode_key(item["target"]["mode"]),
    ))
    return {
        "schema_version": 1,
        "source": {
            "mapping": cartridge["mapping"],
            "payload_size": len(payload),
            "max_blocks_per_processor": max_blocks_per_processor,
            "byte_policy": "instruction-bytes-only-max-4-per-node",
        },
        "regions": regions,
        "blocks": emitted_blocks,
        "edges": emitted_edges,
    }


def _mode_key(mode: Mapping[str, Any]) -> str:
    return f"e{int(mode['emulation'])}m{int(mode['m8'])}x{int(mode['x8'])}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vectors", type=Path)
    parser.add_argument("trace_cfg", type=Path)
    parser.add_argument("rom", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-blocks", type=int, default=256)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        vectors = json.loads(args.vectors.read_text(encoding="utf-8"))
        trace_cfg = json.loads(args.trace_cfg.read_text(encoding="utf-8"))
        result = lift_reset_paths(
            vectors, trace_cfg, args.rom.read_bytes(), max_blocks_per_processor=args.max_blocks
        )
    except (OSError, json.JSONDecodeError, ResetLiftError) as error:
        parser.error(str(error))
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing lifted reset CFG: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
