"""Compare private reference/candidate logs without exporting state values."""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path


STATE_FIELDS = ("sequence", "processor", "cycle", "pc", "a", "x", "y", "d", "sp", "dbr", "ps", "emulation")
WRITE_FIELDS = ("processor", "address", "value")


@dataclass(frozen=True)
class PrivateLog:
    limit: int
    states: tuple[tuple[object, ...], ...]
    writes: tuple[tuple[object, ...], ...]
    reason: str
    scpu_count: int
    sa1_count: int
    digest: str


def _integer(value: str, label: str, maximum: int | None = None) -> int:
    try:
        result = int(value, 10)
    except ValueError as exc:
        raise ValueError(f"{label} must be decimal") from exc
    if result < 0 or (maximum is not None and result > maximum):
        raise ValueError(f"{label} is out of range")
    return result


def parse_log(path: Path) -> PrivateLog:
    raw = path.read_bytes()
    lines = raw.decode("utf-8", errors="strict").splitlines()
    starts = [line for line in lines if line.startswith("KSS_DIFF_START_V1|")]
    ends = [line for line in lines if line.startswith("KSS_DIFF_END_V1|")]
    if len(starts) != 1 or len(ends) != 1:
        raise ValueError("log requires exactly one start and end marker")
    limit = _integer(starts[0].split("|")[1], "limit")
    if not 1 <= limit <= 10000:
        raise ValueError("limit is out of range")

    states: list[tuple[object, ...]] = []
    writes: list[tuple[object, ...]] = []
    for line in lines:
        if line.startswith("KSS_DIFF_STATE_V1|"):
            parts = line.split("|")
            if len(parts) != 13:
                raise ValueError("malformed state record")
            processor = parts[2]
            if processor not in {"scpu", "sa1"}:
                raise ValueError("invalid processor")
            values = (
                _integer(parts[1], "sequence"), processor,
                _integer(parts[3], "cycle"), int(parts[4], 16),
                *(_integer(item, "register", 0xFFFF) for item in parts[5:10]),
                _integer(parts[10], "dbr", 0xFF), _integer(parts[11], "ps", 0xFF),
                _integer(parts[12], "emulation", 1),
            )
            if values[3] > 0xFFFFFF:
                raise ValueError("PC is out of range")
            states.append(values)
        elif line.startswith("KSS_DIFF_WRITE_V1|"):
            parts = line.split("|")
            if len(parts) != 4 or parts[1] not in {"scpu", "sa1"}:
                raise ValueError("malformed write record")
            address = int(parts[2], 16)
            value = _integer(parts[3], "write value", 0xFF)
            if address > 0xFFFFFF:
                raise ValueError("write address is out of range")
            writes.append((parts[1], address, value))
            if len(writes) > 1_000_000:
                raise ValueError("write record limit exceeded")

    end = ends[0].split("|")
    if len(end) != 5:
        raise ValueError("malformed end marker")
    total, reason = _integer(end[1], "total"), end[2]
    scpu_count, sa1_count = _integer(end[3], "S-CPU count"), _integer(end[4], "SA-1 count")
    if reason not in {"limit", "complete"} or total != len(states):
        raise ValueError("end marker does not match state records")
    if scpu_count + sa1_count != total or max(scpu_count, sa1_count) > limit:
        raise ValueError("processor counts do not match state records")
    actual_scpu = sum(state[1] == "scpu" for state in states)
    actual_sa1 = sum(state[1] == "sa1" for state in states)
    if (scpu_count, sa1_count) != (actual_scpu, actual_sa1):
        raise ValueError("processor counts do not match state records")
    if [state[0] for state in states] != list(range(1, len(states) + 1)):
        raise ValueError("state sequence is not contiguous")
    return PrivateLog(limit, tuple(states), tuple(writes), reason, scpu_count, sa1_count, hashlib.sha256(raw).hexdigest())


def compare_logs(reference_path: Path, candidate_path: Path) -> dict[str, object]:
    reference, candidate = parse_log(reference_path), parse_log(candidate_path)
    mismatches: list[dict[str, object]] = []
    for index in range(max(len(reference.states), len(candidate.states))):
        if index >= len(reference.states) or index >= len(candidate.states):
            mismatches.append({"record": "state", "index": index, "fields": ["presence"]})
            continue
        fields = [name for name, left, right in zip(STATE_FIELDS, reference.states[index], candidate.states[index]) if left != right]
        if fields:
            mismatches.append({"record": "state", "index": index, "fields": fields})
    for index in range(max(len(reference.writes), len(candidate.writes))):
        if index >= len(reference.writes) or index >= len(candidate.writes):
            mismatches.append({"record": "write", "index": index, "fields": ["presence"]})
            continue
        fields = [name for name, left, right in zip(WRITE_FIELDS, reference.writes[index], candidate.writes[index]) if left != right]
        if fields:
            mismatches.append({"record": "write", "index": index, "fields": fields})
    return {
        "schema_version": 1,
        "passed": not mismatches,
        "reference_sha256": reference.digest,
        "candidate_sha256": candidate.digest,
        "state_records": len(reference.states),
        "write_records": len(reference.writes),
        "processors": {"scpu": reference.scpu_count, "sa1": reference.sa1_count},
        "mismatch_count": len(mismatches),
        "mismatches": mismatches[:100],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = compare_logs(args.reference, args.candidate)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
