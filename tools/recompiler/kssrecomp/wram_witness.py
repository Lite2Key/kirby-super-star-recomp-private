"""Parse private executable-WRAM byte witnesses.

Mesen's ``KSS_WRAM_BYTES_V1`` marker is intentionally consumed only from the
ignored ``.private`` tree.  The parsed artifact is sparse, bounded, and keeps
the native S-CPU mode explicit so the lifter cannot apply a witness to a
different processor or decode mode by accident.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any, Iterable, Mapping


class WramWitnessError(ValueError):
    """A private WRAM witness log is malformed or contradictory."""


_BYTES = re.compile(r"KSS_WRAM_BYTES_V1\|([0-9A-Fa-f]{6})\|([0-9A-Fa-f]{2,8})")


def _block(pc: int, bytes_hex: str) -> dict[str, object]:
    return {
        "processor": "scpu",
        "pc": pc,
        "mode": {"emulation": False, "m8": False, "x8": False},
        "bytes_hex": bytes_hex.upper(),
    }


def parse_witness_lines(lines: Iterable[str]) -> dict[str, object]:
    """Return a deterministic sparse witness artifact from private log lines."""
    records: dict[int, str] = {}
    for line_number, line in enumerate(lines, 1):
        match = _BYTES.search(line)
        if match is None:
            continue
        pc = int(match.group(1), 16)
        bytes_hex = match.group(2).upper()
        # The current route uses the first 8 KiB of bank $00 WRAM.  Keeping the
        # parser bounded here prevents an accidental ROM/SA-1 witness from
        # silently becoming executable input.
        if pc >= 0x2000:
            raise WramWitnessError(
                f"line {line_number}: WRAM PC must be below $002000, got ${pc:06X}"
            )
        if len(bytes_hex) % 2 or not 1 <= len(bytes_hex) // 2 <= 4:
            raise WramWitnessError(f"line {line_number}: witness must contain 1-4 bytes")
        prior = records.get(pc)
        if prior is not None and prior != bytes_hex:
            raise WramWitnessError(
                f"line {line_number}: contradictory bytes at ${pc:06X}"
            )
        records[pc] = bytes_hex
    if not records:
        raise WramWitnessError("log contains no KSS_WRAM_BYTES_V1 markers")
    blocks = [_block(pc, bytes_hex) for pc, bytes_hex in sorted(records.items())]
    return {
        "schema_version": 1,
        "format": "kss-wram-witness-v1",
        "processor": "scpu",
        "address_space": "cpu24-bank00-wram",
        "blocks": blocks,
    }


def witness_index(witness: Mapping[str, Any] | None) -> dict[tuple[int, bool, bool, bool], bytes]:
    """Index validated witness blocks by PC and native mode."""
    if witness is None:
        return {}
    if witness.get("schema_version") != 1 or witness.get("format") != "kss-wram-witness-v1":
        raise WramWitnessError("unsupported WRAM witness schema")
    if witness.get("processor") != "scpu":
        raise WramWitnessError("WRAM witness processor must be scpu")
    blocks = witness.get("blocks")
    if not isinstance(blocks, list) or not blocks:
        raise WramWitnessError("WRAM witness blocks must be a non-empty array")
    result: dict[tuple[int, bool, bool, bool], bytes] = {}
    for index, raw in enumerate(blocks):
        if not isinstance(raw, Mapping):
            raise WramWitnessError(f"blocks[{index}] is not an object")
        try:
            if raw.get("processor") != "scpu":
                raise WramWitnessError(f"blocks[{index}] processor must be scpu")
            pc = raw["pc"]
            mode = raw["mode"]
            bytes_hex = raw["bytes_hex"]
            if not isinstance(pc, int) or isinstance(pc, bool) or not 0 <= pc < 0x2000:
                raise WramWitnessError(f"blocks[{index}] PC is outside bank $00 WRAM")
            if not isinstance(mode, Mapping) or mode != {
                "emulation": False, "m8": False, "x8": False,
            }:
                raise WramWitnessError(f"blocks[{index}] must be native 16-bit mode")
            if not isinstance(bytes_hex, str) or len(bytes_hex) not in (2, 4, 6, 8):
                raise WramWitnessError(f"blocks[{index}] bytes_hex must contain 1-4 bytes")
            value = bytes.fromhex(bytes_hex)
        except (KeyError, TypeError, ValueError) as error:
            raise WramWitnessError(f"invalid blocks[{index}]: {error}") from error
        key = (pc, False, False, False)
        prior = result.get(key)
        if prior is not None and prior != value:
            raise WramWitnessError(f"contradictory bytes for ${pc:06X}")
        result[key] = value
    return result


def load_witness(path: Path) -> dict[str, object]:
    try:
        return parse_witness_lines(path.read_text(encoding="utf-8", errors="replace").splitlines())
    except OSError as error:
        raise WramWitnessError(str(error)) from error


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="private Mesen log containing KSS_WRAM_BYTES_V1 markers")
    parser.add_argument("output", type=Path, help="private JSON witness artifact")
    args = parser.parse_args(argv)
    try:
        artifact = load_witness(args.input)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, WramWitnessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
