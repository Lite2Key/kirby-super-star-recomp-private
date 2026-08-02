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
from .wram_witness import WramWitnessError, witness_index


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
    bank = (address >> 16) & 0xFF
    if bank >= 0xC0:
        # SA-1 cartridges expose the ROM linearly through banks $C0-$FF.
        return (((bank & 0x3F) << 16) | (address & 0xFFFF)) % payload_size
    if address & 0xFFFF < 0x8000:
        return None
    return ((((address >> 16) & 0x7F) << 15) | (address & 0x7FFF)) % payload_size


def _edge_is_supported(
    flow: Flow, pc: int, size: int, target: int | None, successor: int, opcode: int | None = None,
    *, allow_observed_dynamic_control_flow: bool = False,
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
        return (target is not None and successor == target) or (
            allow_observed_dynamic_control_flow and target is None
        )
    if allow_observed_dynamic_control_flow and flow in (
        Flow.RETURN, Flow.INTERRUPT, Flow.INTERRUPT_RETURN,
    ):
        # The sanitized route supplies only the resulting identity. Generated
        # execution computes the target from runtime stack/vector state; the
        # dispatcher remains fail-closed when that identity is not registered.
        return True
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


def _mode_dict(mode: CpuMode) -> dict[str, bool]:
    """Serialize a decoder mode for polymorphic status restores.

    RTI/PLP restore processor flags from the stack.  A trace identity records
    the resulting mode, but the same instruction identity can legitimately
    have several resulting modes.  Keep that set explicit instead of
    selecting one mode and silently widening the generated successor check.
    """
    return {
        "emulation": mode.emulation,
        "m8": mode.m8,
        "x8": mode.x8,
    }


def _native_nmi_targets(vectors: Mapping[str, Any]) -> set[int]:
    """Return declared native S-CPU NMI entries, if the vector artifact has one."""
    raw_vectors = vectors.get("processors", {}).get("scpu", {}).get("vectors", [])
    if not isinstance(raw_vectors, list):
        return set()
    return {
        int(item["target_cpu_address"])
        for item in raw_vectors
        if isinstance(item, Mapping)
        and item.get("name") == "native_nmi"
        and isinstance(item.get("target_cpu_address"), int)
    }


def _is_async_native_nmi_edge(
    source: BlockIdentity,
    target: BlockIdentity,
    instruction: Any,
    native_nmi_targets: set[int],
) -> bool:
    """Recognize an interrupt transition that is not an ISA successor.

    NMI can be accepted only at an instruction boundary, so the mode observed
    at the handler must equal the completed instruction's mode.  A direct
    branch/call/jump to the same address remains an ordinary successor.
    """
    has_static_successor_model = instruction.flow in (Flow.NEXT, Flow.BRANCH) or (
        instruction.flow in (Flow.CALL, Flow.JUMP) and instruction.target is not None
    )
    return (
        source.processor == Processor.SCPU
        and target.processor == Processor.SCPU
        and has_static_successor_model
        and target.pc in native_nmi_targets
        and target.mode == instruction.state_after.mode
        and not _edge_is_supported(
            instruction.flow,
            source.pc,
            instruction.size,
            instruction.target,
            target.pc,
            instruction.opcode,
            allow_observed_dynamic_control_flow=False,
        )
    )


@dataclass(frozen=True)
class _State:
    carry: bool | None


def lift_reset_paths(
    vectors: Mapping[str, Any],
    trace_cfg: Mapping[str, Any],
    rom: bytes,
    *,
    max_blocks_per_processor: int = 256,
    allow_observed_dynamic_control_flow: bool = False,
    analyze_all_observed_identities: bool = False,
    wram_witness: Mapping[str, Any] | None = None,
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
    try:
        wram_index = witness_index(wram_witness)
    except WramWitnessError as error:
        raise ResetLiftError(f"invalid WRAM witness: {error}") from error

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
    routes_by_identity: dict[BlockIdentity, list[str]] = {}
    for raw in trace_cfg.get("observations", {}).get("blocks", []):
        identity = _identity(raw["identity"])
        routes = raw.get("routes", [])
        if not isinstance(routes, list) or any(not isinstance(route, str) for route in routes):
            raise ResetLiftError("trace CFG block provenance is invalid")
        routes_by_identity[identity] = list(routes)
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
    emitted_async_edges: list[dict[str, object]] = []
    native_nmi_targets = _native_nmi_targets(vectors)
    regions: dict[str, dict[str, object]] = {}
    for processor in Processor:
        entry = entries[processor]
        pending = deque([entry])
        if analyze_all_observed_identities:
            pending.extend(sorted(
                (identity for identity in identities
                 if identity.processor == processor and identity != entry),
                key=lambda item: (item.pc, item.mode.key),
            ))
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
            if routes_by_identity.get(identity):
                record["routes"] = routes_by_identity[identity]
            if offset is None:
                # The first-visible route contains a small native S-CPU
                # trampoline copied into bank $00 WRAM at runtime.  Its bytes
                # are accepted only from an explicit private witness; no
                # public artifact can cause the lifter to guess RAM contents.
                witness_key = (
                    identity.pc,
                    identity.mode.emulation,
                    identity.mode.m8,
                    identity.mode.x8,
                )
                witness_bytes = (
                    wram_index.get(witness_key)
                    if identity.processor == Processor.SCPU
                    else None
                )
                if witness_bytes is None:
                    record["status"] = "unresolved"
                    record["unresolved_reason"] = "address_not_rom_mapped"
                    processor_blocks.append(record)
                    continue
                record["memory_region"] = "wram"
                instruction_bytes = witness_bytes
            else:
                instruction_bytes = payload[offset : offset + 4]
            targets = sorted(outgoing[identity], key=lambda item: (item.pc, item.mode.key))
            state = DecoderState(identity.mode, states.get(identity, _State(None)).carry)
            restored_state = None
            restored_modes: list[CpuMode] = []
            if allow_observed_dynamic_control_flow and instruction_bytes[0] in (0x28, 0x40):
                # PLP/RTI restore the mode from the stacked status byte.  A
                # single static identity can therefore have several observed
                # successor modes.  Decode using a deterministic representative
                # mode, but retain the complete observed set on the block and
                # validate every edge against it below.  Generated execution
                # still registers the observed identities, so an unregistered
                # stack-restored target fails closed at dispatch.
                restored_modes = sorted({target.mode for target in targets}, key=lambda mode: mode.key)
                # The emulation flag is not part of the stacked status byte;
                # accepting an observed target that changes it would turn a
                # malformed trace into an executable mode transition.
                if any(mode.emulation != identity.mode.emulation for mode in restored_modes):
                    restored_modes = []
                if restored_modes:
                    restored_state = DecoderState(restored_modes[0], None)
                    record["restored_modes"] = [_mode_dict(mode) for mode in restored_modes]
            try:
                instruction = decode_one(
                    instruction_bytes[:4], identity.pc, state,
                    restored_state=restored_state,
                )
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
            if not allow_observed_dynamic_control_flow and (
                instruction.flow in (Flow.RETURN, Flow.INTERRUPT, Flow.INTERRUPT_RETURN) or (
                instruction.flow in (Flow.CALL, Flow.JUMP) and instruction.target is None
                )
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
            async_nmi_targets = [
                target for target in targets
                if _is_async_native_nmi_edge(identity, target, instruction, native_nmi_targets)
            ]
            instruction_targets = [target for target in targets if target not in async_nmi_targets]
            # A vector-only observation cannot prove the instruction's normal
            # successor. Keep that case unresolved instead of silently
            # widening the generated block's accepted control flow.
            if async_nmi_targets and not instruction_targets:
                instruction_targets = targets
                async_nmi_targets = []

            accepted: list[BlockIdentity] = []
            mismatch = False
            for target in instruction_targets:
                mode_matches = (
                    target.mode in restored_modes
                    if restored_modes
                    else target.mode == instruction.state_after.mode
                )
                if not mode_matches or not _edge_is_supported(
                    instruction.flow, identity.pc, instruction.size, instruction.target, target.pc,
                    instruction.opcode,
                    allow_observed_dynamic_control_flow=allow_observed_dynamic_control_flow,
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
            for target in async_nmi_targets:
                emitted_async_edges.append({
                    "source": identity.to_dict(),
                    "target": target.to_dict(),
                    "kind": "asynchronous_interrupt",
                    "vector": "native_nmi",
                })
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
    emitted_async_edges.sort(key=lambda item: (
        item["source"]["processor"], item["source"]["pc"], _mode_key(item["source"]["mode"]),
        item["target"]["pc"], _mode_key(item["target"]["mode"]), item["vector"],
    ))
    return {
        "schema_version": 1,
        "source": {
            "mapping": cartridge["mapping"],
            "payload_size": len(payload),
            "max_blocks_per_processor": max_blocks_per_processor,
            "byte_policy": "instruction-bytes-only-max-4-per-node",
            **({"wram_witness_policy": "private-sparse-bytes-v1"} if wram_index else {}),
            "dynamic_control_flow_policy": (
                "observed-successors-fail-closed"
                if allow_observed_dynamic_control_flow
                else "unresolved"
            ),
            "selection_policy": (
                "all-observed-decoded"
                if analyze_all_observed_identities
                else "entry-reachable-decoded"
            ),
        },
        "regions": regions,
        "blocks": emitted_blocks,
        "edges": emitted_edges,
        "async_edges": emitted_async_edges,
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
