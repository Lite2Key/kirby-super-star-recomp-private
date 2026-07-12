#!/usr/bin/env python3
"""Write sanitized reset/vector metadata; ROM content remains private."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from recompiler.kssrecomp.errors import RomFormatError
from recompiler.kssrecomp.vectors import (
    extract_vectors,
    load_observed_processor_entry,
    load_observed_sa1_reset,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path, help="private, authorized ROM path")
    parser.add_argument("output", type=Path, help="sanitized JSON output")
    parser.add_argument("--scpu-trace", type=Path, help="sanitized Mesen coverage JSON used to confirm reset")
    parser.add_argument("--sa1-trace", type=Path, help="sanitized Mesen coverage JSON")
    args = parser.parse_args()

    sa1_pc = load_observed_sa1_reset(args.sa1_trace) if args.sa1_trace else None
    result = extract_vectors(args.rom.read_bytes(), sa1_reset_pc=sa1_pc)
    if args.scpu_trace:
        observed = load_observed_processor_entry(args.scpu_trace, "scpu")
        static_reset = next(
            item["target_cpu_address"]
            for item in result["processors"]["scpu"]["vectors"]
            if item["name"] == "emulation_reset"
        )
        if observed != static_reset:
            raise RomFormatError(
                f"S-CPU trace starts at 0x{observed:06X}, not reset vector 0x{static_reset:06X}"
            )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
