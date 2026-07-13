"""Sanitize a private first-frame SPC700 execution and CPU-port oracle."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from typing import Any


class SpcOracleError(ValueError):
    """The SPC oracle is malformed, incomplete, or internally inconsistent."""


def _int(text: str, label: str, maximum: int | None = None) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise SpcOracleError(f"invalid {label}") from exc
    if value < 0 or (maximum is not None and value > maximum):
        raise SpcOracleError(f"{label} is outside its allowed range")
    return value


def _hex16(text: str) -> int:
    if len(text) != 4 or any(char not in "0123456789abcdefABCDEF" for char in text):
        raise SpcOracleError("invalid SPC address")
    return int(text, 16)


def _digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
    ).hexdigest()


def _summarize_v2(raw: bytes, records: list[str]) -> dict[str, Any]:
    """Parse the phase-complete format without exporting captured values."""
    if any(line.startswith("KSS_SPC_ABORT_V2|") for line in records):
        raise SpcOracleError("SPC oracle aborted")
    starts = [line for line in records if line.startswith("KSS_SPC_START_V2|")]
    finals = [line for line in records if line.startswith("KSS_SPC_FINAL_V2|")]
    ends = [line for line in records if line.startswith("KSS_SPC_END_V2|")]
    if len(starts) != 1 or len(finals) != 1 or len(ends) != 1:
        raise SpcOracleError("SPC oracle requires one start, final, and end marker")
    if records[0] != starts[0] or records[-1] != ends[0] or records[-2] != finals[0]:
        raise SpcOracleError("SPC markers do not bound every private record")
    start = starts[0].split("|")
    if len(start) != 3:
        raise SpcOracleError("malformed SPC start marker")
    exec_limit, io_limit = _int(start[1], "exec limit"), _int(start[2], "I/O limit")
    if not 1 <= exec_limit <= 100_000 or not 1 <= io_limit <= 20_000:
        raise SpcOracleError("SPC capture bounds exceed sanitizer limits")

    executions: list[tuple[int, ...]] = []
    ios: list[tuple[Any, ...]] = []
    event_ordinals: list[int] = []
    master_clocks: list[int] = []
    kinds = {"scpu_port_write", "spc_port_read", "spc_port_write"}
    for line in records[1:-2]:
        parts = line.split("|")
        if parts[0] == "KSS_SPC_EXEC_V2":
            if len(parts) != 12:
                raise SpcOracleError("malformed SPC execution record")
            item = (
                _int(parts[1], "event ordinal"), _int(parts[2], "exec ordinal"),
                _int(parts[3], "master clock"), _int(parts[4], "SPC cycle"),
                _hex16(parts[5]), _int(parts[6], "opcode", 0xff),
                _int(parts[7], "A", 0xff), _int(parts[8], "X", 0xff),
                _int(parts[9], "Y", 0xff), _int(parts[10], "SP", 0xff),
                _int(parts[11], "P", 0xff),
            )
            if item[1] != len(executions) + 1:
                raise SpcOracleError("SPC execution ordinals are not contiguous")
            executions.append(item)
        elif parts[0] == "KSS_SPC_IO_V2":
            if len(parts) != 9 or parts[3] not in kinds:
                raise SpcOracleError("malformed SPC I/O record")
            item = (
                _int(parts[1], "event ordinal"), _int(parts[2], "I/O ordinal"),
                parts[3], _int(parts[4], "master clock"),
                _int(parts[5], "SPC cycle"), _int(parts[6], "S-CPU cycle"),
                _int(parts[7], "port index", 3), _int(parts[8], "port value", 0xff),
            )
            if item[1] != len(ios) + 1:
                raise SpcOracleError("SPC I/O ordinals are not contiguous")
            ios.append(item)
        else:
            raise SpcOracleError("unknown private SPC record")
        event_ordinals.append(item[0])
        master_clocks.append(item[2] if parts[0] == "KSS_SPC_EXEC_V2" else item[3])
    if event_ordinals != list(range(1, len(event_ordinals) + 1)):
        raise SpcOracleError("SPC event ordinals are not contiguous")
    if master_clocks != sorted(master_clocks):
        raise SpcOracleError("SPC master clocks are not monotonic")

    final = finals[0].split("|")
    if len(final) != 10:
        raise SpcOracleError("malformed SPC final-state marker")
    final_state = (
        _int(final[1], "final event ordinal"), _int(final[2], "final master clock"),
        _int(final[3], "final SPC cycle"), _hex16(final[4]),
        _int(final[5], "final A", 0xff), _int(final[6], "final X", 0xff),
        _int(final[7], "final Y", 0xff), _int(final[8], "final SP", 0xff),
        _int(final[9], "final P", 0xff),
    )
    if final_state[0] != len(event_ordinals) + 1:
        raise SpcOracleError("final event ordinal is not contiguous")
    if master_clocks and final_state[1] < master_clocks[-1]:
        raise SpcOracleError("final master clock precedes captured events")
    end = ends[0].split("|")
    if len(end) != 7 or end[1] != "first_end_frame":
        raise SpcOracleError("malformed SPC end marker")
    if _int(end[2], "exec count") != len(executions) or len(executions) > exec_limit:
        raise SpcOracleError("SPC end exec count disagrees with records")
    if _int(end[3], "I/O count") != len(ios) or len(ios) > io_limit:
        raise SpcOracleError("SPC end I/O count disagrees with records")
    if _int(end[4], "event count") != final_state[0]:
        raise SpcOracleError("SPC end event count disagrees with records")
    if _int(end[5], "master clock") != final_state[1]:
        raise SpcOracleError("SPC final master clock disagrees with end marker")
    if _int(end[6], "end SPC cycle") != final_state[2]:
        raise SpcOracleError("SPC final cycle disagrees with end marker")
    if not executions:
        raise SpcOracleError("SPC oracle has no executions")

    opcode_counts = Counter(item[5] for item in executions)
    io_counts = Counter((item[2], item[6]) for item in ios)
    ties = Counter(master_clocks)
    return {
        "schema_version": 2,
        "source_format": "kss-spc-first-frame-v2",
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "boundary": "first_snes_end_frame",
        "clocks": {
            "master": final_state[1],
            "spc_counter_start": executions[0][3],
            "spc_counter_end": final_state[2],
            "spc_counter_delta": final_state[2] - executions[0][3],
            "counter_unit": "mesen_spc_cycle_counter",
        },
        "execution": {
            "records": len(executions),
            "unique_pcs": len({item[4] for item in executions}),
            "first_pc": executions[0][4],
            "final_pc": final_state[3],
            "opcodes": [{"opcode": op, "records": opcode_counts[op]} for op in sorted(opcode_counts)],
            "bootstrap_required_before_hook": [0xcd, 0xbd, 0xe8],
            "state_chain_sha256": _digest(executions),
            "final_state_sha256": _digest(final_state),
        },
        "io": {
            "records": len(ios),
            "by_kind_and_index": [
                {"kind": kind, "index": index, "records": io_counts[(kind, index)]}
                for kind, index in sorted(io_counts)
            ],
            "chain_sha256": _digest(ios),
        },
        "phase_order": {
            "records": final_state[0],
            "same_master_clock_groups": sum(1 for count in ties.values() if count > 1),
            "callback_ordinal_chain_sha256": _digest(event_ordinals + [final_state[0]]),
        },
    }


def summarize_spc_oracle(path: Path) -> dict[str, Any]:
    raw = path.read_bytes()
    try:
        lines = raw.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise SpcOracleError("SPC oracle is not UTF-8") from exc
    records = [line for line in lines if line.startswith("KSS_SPC_")]
    if any(line.startswith("KSS_SPC_START_V2|") for line in records):
        return _summarize_v2(raw, records)
    if any(line.startswith("KSS_SPC_ABORT_V1|") for line in records):
        raise SpcOracleError("SPC oracle aborted")
    starts = [line for line in records if line.startswith("KSS_SPC_START_V1|")]
    finals = [line for line in records if line.startswith("KSS_SPC_FINAL_V1|")]
    ends = [line for line in records if line.startswith("KSS_SPC_END_V1|")]
    if len(starts) != 1 or len(finals) != 1 or len(ends) != 1:
        raise SpcOracleError("SPC oracle requires one start, final, and end marker")
    if records[0] != starts[0] or records[-1] != ends[0]:
        raise SpcOracleError("SPC markers do not bound every private record")
    start = starts[0].split("|")
    if len(start) != 3:
        raise SpcOracleError("malformed SPC start marker")
    exec_limit, port_limit = _int(start[1], "exec limit"), _int(start[2], "port limit")
    if not 1 <= exec_limit <= 100_000 or not 1 <= port_limit <= 10_000:
        raise SpcOracleError("SPC capture bounds exceed sanitizer limits")

    executions: list[tuple[int, ...]] = []
    ports: list[tuple[Any, ...]] = []
    for line in records[1:-2]:
        parts = line.split("|")
        if parts[0] == "KSS_SPC_EXEC_V1":
            if len(parts) != 10:
                raise SpcOracleError("malformed SPC execution record")
            item = (
                _int(parts[1], "exec ordinal"), _int(parts[2], "SPC cycle"),
                _hex16(parts[3]), _int(parts[4], "opcode", 0xff),
                _int(parts[5], "A", 0xff), _int(parts[6], "X", 0xff),
                _int(parts[7], "Y", 0xff), _int(parts[8], "SP", 0xff),
                _int(parts[9], "P", 0xff),
            )
            if item[0] != len(executions) + 1:
                raise SpcOracleError("SPC execution ordinals are not contiguous")
            executions.append(item)
        elif parts[0] == "KSS_SPC_PORT_V1":
            if len(parts) != 6 or parts[2] not in {"cpu_to_spc", "spc_to_cpu"}:
                raise SpcOracleError("malformed SPC port record")
            item = (
                _int(parts[1], "port ordinal"), parts[2], _int(parts[3], "port cycle"),
                _int(parts[4], "port index", 3), _int(parts[5], "port value", 0xff),
            )
            if item[0] != len(ports) + 1:
                raise SpcOracleError("SPC port ordinals are not contiguous")
            ports.append(item)
        else:
            raise SpcOracleError("unknown private SPC record")

    final = finals[0].split("|")
    if len(final) != 8:
        raise SpcOracleError("malformed SPC final-state marker")
    final_state = (
        _int(final[1], "final SPC cycle"), _hex16(final[2]),
        _int(final[3], "final A", 0xff), _int(final[4], "final X", 0xff),
        _int(final[5], "final Y", 0xff), _int(final[6], "final SP", 0xff),
        _int(final[7], "final P", 0xff),
    )
    end = ends[0].split("|")
    if len(end) != 6 or end[1] != "first_end_frame":
        raise SpcOracleError("malformed SPC end marker")
    if _int(end[2], "exec count") != len(executions) or len(executions) > exec_limit:
        raise SpcOracleError("SPC end exec count disagrees with records")
    if _int(end[3], "port count") != len(ports) or len(ports) > port_limit:
        raise SpcOracleError("SPC end port count disagrees with records")
    master_clock = _int(end[4], "master clock")
    end_cycle = _int(end[5], "end SPC cycle")
    if not executions or final_state[0] != end_cycle:
        raise SpcOracleError("SPC final cycle disagrees with end marker")

    opcode_counts = Counter(item[3] for item in executions)
    port_counts = Counter((item[1], item[3]) for item in ports)
    return {
        "schema_version": 1,
        "source_format": "kss-spc-first-frame-v1",
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "boundary": "first_snes_end_frame",
        "clocks": {
            "master": master_clock,
            "spc_counter_start": executions[0][1],
            "spc_counter_end": end_cycle,
            "spc_counter_delta": end_cycle - executions[0][1],
            "counter_unit": "mesen_spc_cycle_counter",
        },
        "execution": {
            "records": len(executions),
            "unique_pcs": len({item[2] for item in executions}),
            "first_pc": executions[0][2],
            "final_pc": final_state[1],
            "opcodes": [
                {"opcode": opcode, "records": opcode_counts[opcode]}
                for opcode in sorted(opcode_counts)
            ],
            "bootstrap_required_before_hook": [0xcd, 0xbd, 0xe8],
            "state_chain_sha256": _digest(executions),
            "final_state_sha256": _digest(final_state),
        },
        "ports": {
            "records": len(ports),
            "by_direction_and_index": [
                {"direction": direction, "index": index, "records": port_counts[(direction, index)]}
                for direction, index in sorted(port_counts)
            ],
            "chain_sha256": _digest(ports),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_spc_oracle(args.input), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing SPC oracle summary: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
