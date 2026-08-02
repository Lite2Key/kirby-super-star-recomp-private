"""Sanitize the private post-reset SA-1 identity/reference evidence.

The first-frame execution log establishes which processor/mode identities ran
and their order.  The existing TCD slice additionally establishes full
architectural-state and write-chain evidence for the later part of that route.
Only identities, counts, and cryptographic digests leave the private boundary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
from typing import Any

from differential.kss_diff.sa1_slice import Sa1SliceError, _parse as parse_sa1_slice


RESET_REFERENCE_END_CYCLE = 22_011
EXPECTED_TAIL_PCS = (
    0x008C26, 0x008C29, 0x008C2C, 0x008C2F, 0x008C32, 0x008C33,
    0x008C36, 0x008C37, 0x008C3A, 0x008C3D, 0x008C40, 0x008C43,
    0x008C46, 0x008C49, 0x008C4C, 0x008C4F, 0x008C52, 0x008C55,
    0x008C58, 0x008C5B,
)
EXPECTED_STATE_PCS = EXPECTED_TAIL_PCS[6:]

START = re.compile(r"^KSS_TRACE_START_V1\|(\d+)$")
EVENT = re.compile(
    r"^KSS_TRACE_V1\|(\d+)\|(scpu|sa1)\|(\d+)\|([0-9A-Fa-f]{6})"
    r"\|([01])\|([01])\|([01])$"
)
FRAME = re.compile(r"^KSS_TRACE_FIRST_FRAME_V1\|(\d+)\|(\d+)\|(\d+)$")
END = re.compile(r"^KSS_TRACE_END_V1\|(\d+)\|complete$")


class Sa1TailError(ValueError):
    """The private trace cannot prove the bounded SA-1 tail contract."""


def _digest(value: object) -> str:
    encoded = json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _identity(pc: int) -> dict[str, Any]:
    return {
        "processor": "sa1",
        "pc": pc,
        "mode": {"emulation": False, "m8": False, "x8": False},
    }


def _parse_trace(path: Path) -> tuple[str, list[tuple[int, int, int, int]]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as exc:
        raise Sa1TailError("trace is unavailable or not UTF-8") from exc
    records = [line for line in lines if line.startswith("KSS_TRACE_")]
    starts = [line for line in records if START.fullmatch(line)]
    frames = [line for line in records if FRAME.fullmatch(line)]
    ends = [line for line in records if END.fullmatch(line)]
    if len(starts) != 1 or len(frames) != 1 or len(ends) != 1:
        raise Sa1TailError("trace requires one start, first-frame, and complete end marker")
    if records[0] != starts[0] or records[-2:] != [frames[0], ends[0]]:
        raise Sa1TailError("trace markers do not bound the execution records")

    limit = int(START.fullmatch(starts[0]).group(1))  # type: ignore[union-attr]
    events: list[tuple[int, str, int, int, int, int, int]] = []
    prior_cycle: dict[str, int] = {}
    for line in records[1:-2]:
        match = EVENT.fullmatch(line)
        if match is None:
            raise Sa1TailError("trace contains an unknown or malformed record")
        sequence, processor, cycle, pc, emulation, m8, x8 = match.groups()
        event = (
            int(sequence), processor, int(cycle), int(pc, 16),
            int(emulation), int(m8), int(x8),
        )
        if event[0] != len(events) + 1:
            raise Sa1TailError("trace event ordinals are not contiguous")
        if event[2] < prior_cycle.get(processor, 0):
            raise Sa1TailError(f"{processor} cycle count moved backwards")
        if event[4] and not (event[5] and event[6]):
            raise Sa1TailError("emulation mode requires 8-bit accumulator and index modes")
        prior_cycle[processor] = event[2]
        events.append(event)

    frame_match = FRAME.fullmatch(frames[0])
    end_match = END.fullmatch(ends[0])
    assert frame_match is not None and end_match is not None
    total, scpu_count, sa1_count = map(int, frame_match.groups())
    observed_scpu = sum(event[1] == "scpu" for event in events)
    observed_sa1 = sum(event[1] == "sa1" for event in events)
    if total != len(events) or int(end_match.group(1)) != len(events):
        raise Sa1TailError("trace end counts disagree with execution records")
    if (scpu_count, sa1_count) != (observed_scpu, observed_sa1):
        raise Sa1TailError("processor counts disagree with execution records")
    if not 1 <= len(events) <= limit <= 1_000_000:
        raise Sa1TailError("trace exceeds its declared sanitizer bound")

    tail = [
        (event[3], event[4], event[5], event[6])
        for event in events
        if event[1] == "sa1" and event[2] > RESET_REFERENCE_END_CYCLE
    ]
    if not tail:
        raise Sa1TailError("trace has no SA-1 execution after the reset reference")
    unique = tuple(dict.fromkeys(item[0] for item in tail))
    if unique != EXPECTED_TAIL_PCS:
        raise Sa1TailError("post-reset SA-1 identity route is incomplete or reordered")
    if any(item[1:] != (0, 0, 0) for item in tail):
        raise Sa1TailError("post-reset SA-1 route left native 16-bit mode")
    source_hash = hashlib.sha256(("\n".join(records) + "\n").encode("utf-8")).hexdigest()
    return source_hash, tail


def summarize_sa1_tail(trace_path: Path, slice_path: Path) -> dict[str, Any]:
    """Return value-free reference coverage for the 20-identity SA-1 tail."""
    trace_hash, tail = _parse_trace(trace_path)
    try:
        slice_hash, states, writes, _poll = parse_sa1_slice(slice_path)
    except Sa1SliceError as exc:
        raise Sa1TailError(str(exc)) from exc
    state_pcs = tuple(dict.fromkeys(state[2] for state in states if state[2] in EXPECTED_TAIL_PCS))
    if state_pcs != EXPECTED_STATE_PCS:
        raise Sa1TailError("architectural slice does not cover the expected SA-1 tail identities")

    route_identities = [_identity(pc) for pc in EXPECTED_TAIL_PCS]
    state_identities = [_identity(pc) for pc in EXPECTED_STATE_PCS]
    identity_only = [_identity(pc) for pc in EXPECTED_TAIL_PCS[:6]]
    return {
        "schema_version": 1,
        "source_format": "kss-sa1-post-reset-reference-v1",
        "processor": "sa1",
        "boundary": "after-reset-reference-through-first-end-frame",
        "claim_scope": (
            "all listed identities have Mesen execution-order reference evidence; "
            "only architectural_slice identities also have per-instruction state/write evidence"
        ),
        "private_source_sha256": {
            "execution_trace": trace_hash,
            "architectural_slice": slice_hash,
        },
        "identity_route": {
            "observed_identity_count": len(route_identities),
            "event_records": len(tail),
            "identity_chain_sha256": _digest(tail),
            "identities": route_identities,
        },
        "architectural_slice": {
            "state_verified_count": len(state_identities),
            "state_records": len(states),
            "write_records": len(writes),
            "state_chain_sha256": _digest([state[1:] for state in states]),
            "write_chain_sha256": _digest(writes),
            "identities": state_identities,
        },
        "identity_order_only_remaining": {
            "identity_order_only_count": len(identity_only),
            "reason": "existing private trace has identity/order but no per-instruction state records",
            "identities": identity_only,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="private first-frame execution trace")
    parser.add_argument("slice", type=Path, help="private SA-1 architectural slice")
    parser.add_argument("output", type=Path, help="committed sanitized JSON")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_sa1_tail(args.trace, args.slice), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing SA-1 tail reference: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
