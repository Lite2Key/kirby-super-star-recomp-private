"""Build ROM-free S-CPU reference-oracle identity coverage."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from differential.kss_diff.frontier_oracle import summarize_frontier, summarize_second_frontier


Identity = tuple[int, bool, bool, bool]


class ReferenceCoverageError(ValueError):
    """Raised when the sanitized reference windows do not cover the inventory."""


def _identity(item: dict[str, Any]) -> Identity:
    try:
        mode = item["identity"]["mode"]
        return (
            item["identity"]["pc"],
            mode["emulation"],
            mode["m8"],
            mode["x8"],
        )
    except (KeyError, TypeError) as exc:
        raise ReferenceCoverageError("malformed first-frame identity") from exc


def _public_identity(identity: Identity, proofs: list[str]) -> dict[str, Any]:
    pc, emulation, m8, x8 = identity
    return {
        "processor": "scpu",
        "pc": pc,
        "mode": {"emulation": emulation, "m8": m8, "x8": x8},
        "proofs": proofs,
    }


def _state_identities(path: Path, marker: str) -> set[Identity]:
    prefix = f"KSS_{marker}_STATE_V1|"
    identities: set[Identity] = set()
    ordinals: list[int] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith(prefix):
            continue
        parts = line.split("|")
        if len(parts) != 14:
            raise ReferenceCoverageError(f"malformed {marker} state record")
        ordinal = int(parts[1])
        pc = int(parts[3], 16)
        status = int(parts[11])
        emulation = bool(int(parts[12]))
        if pc > 0xFFFFFF or status > 0xFF:
            raise ReferenceCoverageError(f"out-of-range {marker} state record")
        ordinals.append(ordinal)
        identities.add((pc, emulation, emulation or bool(status & 0x20),
                        emulation or bool(status & 0x10)))
    if ordinals != list(range(1, len(ordinals) + 1)) or not identities:
        raise ReferenceCoverageError(f"non-contiguous or empty {marker} state chain")
    return identities


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def build_reference_identity_coverage(
    inventory_path: Path,
    reset_reference_path: Path,
    first_frontier_log: Path,
    first_frontier_reference_path: Path,
    second_frontier_log: Path,
    second_frontier_reference_path: Path,
) -> dict[str, Any]:
    """Sanitize three Mesen reference windows into identity-only coverage."""
    inventory_document = _load_json(inventory_path)
    observations = inventory_document["observations"]["blocks"]
    scpu_observations = [
        item for item in observations if item["identity"]["processor"] == "scpu"
    ]
    inventory = {_identity(item) for item in scpu_observations}
    if len(inventory) != len(scpu_observations):
        raise ReferenceCoverageError("S-CPU inventory contains duplicate identities")

    reset_reference = _load_json(reset_reference_path)
    reset_blocks = [item for item in reset_reference["blocks"] if item["processor"] == "scpu"]
    if len(reset_blocks) != 1:
        raise ReferenceCoverageError("reset reference must contain one S-CPU window")
    cycle_start = reset_blocks[0]["cycle_start"]
    cycle_end = reset_blocks[0]["cycle_end"]
    reset_identities = {
        _identity(item)
        for item in scpu_observations
        if cycle_start <= item["first_cycle"] <= cycle_end
    }

    first_summary = summarize_frontier(first_frontier_log)
    second_summary = summarize_second_frontier(second_frontier_log)
    if first_summary != _load_json(first_frontier_reference_path):
        raise ReferenceCoverageError("first private frontier does not match its committed summary")
    if second_summary != _load_json(second_frontier_reference_path):
        raise ReferenceCoverageError("second private frontier does not match its committed summary")

    first_identities = _state_identities(first_frontier_log, "FRONTIER")
    second_identities = _state_identities(second_frontier_log, "FRONTIER3")
    oracle_identities = reset_identities | first_identities | second_identities
    outside_inventory = oracle_identities - inventory
    if outside_inventory:
        raise ReferenceCoverageError("reference windows contain identities outside the inventory")
    remaining = inventory - oracle_identities
    if remaining:
        raise ReferenceCoverageError("reference windows do not cover the complete S-CPU inventory")

    tail = inventory - reset_identities
    proof_paths = {
        "reset": "analysis/differential/reset-block-reference.json",
        "frontier": "analysis/differential/scpu-frontier-reference.json",
        "frontier2": "analysis/differential/scpu-frontier2-reference.json",
    }
    ordered_tail = sorted(tail, key=lambda item: (item[0], item[1:]))
    identities = []
    for identity in ordered_tail:
        proofs = []
        if identity in first_identities:
            proofs.append(proof_paths["frontier"])
        if identity in second_identities:
            proofs.append(proof_paths["frontier2"])
        identities.append(_public_identity(identity, proofs))

    return {
        "schema_version": 1,
        "claim_scope": (
            "ROM-free MesenCE architectural-state reference-oracle identity coverage; "
            "this records oracle availability, not native state or timing parity"
        ),
        "identity_policy": "processor-pc24-emulation-m8-x8",
        "inventory_source": "analysis/cfg/first-frame-dual.trace-cfg.json",
        "inventory_count": len(inventory),
        "baseline_reference_verified_count": len(reset_identities),
        "newly_reference_verified_count": len(tail),
        "reference_verified_count": len(oracle_identities),
        "proof_windows": [
            {
                "summary": proof_paths["reset"],
                "identity_count": len(reset_identities),
                "new_identity_count": len(reset_identities),
            },
            {
                "summary": proof_paths["frontier"],
                "identity_count": len(first_identities),
                "new_identity_count": len(first_identities - reset_identities),
            },
            {
                "summary": proof_paths["frontier2"],
                "identity_count": len(second_identities),
                "new_identity_count": len(second_identities - reset_identities - first_identities),
            },
        ],
        "newly_reference_verified_identities": identities,
        "remaining_unverified_identities": [],
        "rom_bytes_included": False,
        "private_values_included": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("reset_reference", type=Path)
    parser.add_argument("first_frontier_log", type=Path)
    parser.add_argument("first_frontier_reference", type=Path)
    parser.add_argument("second_frontier_log", type=Path)
    parser.add_argument("second_frontier_reference", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = build_reference_identity_coverage(
        args.inventory,
        args.reset_reference,
        args.first_frontier_log,
        args.first_frontier_reference,
        args.second_frontier_log,
        args.second_frontier_reference,
    )
    rendered = json.dumps(result, indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing reference identity coverage: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
