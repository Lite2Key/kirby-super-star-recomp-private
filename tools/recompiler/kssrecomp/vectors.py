"""Extract ROM-free reset/vector metadata from an authorized private ROM.

Only decoded addresses and cartridge mapping facts leave this module.  It never
returns ROM payload bytes, instruction bytes, strings, or disassembly.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .errors import RomFormatError
from .rom import _strip_copier_header, inspect


_VECTOR_FIELDS = (
    ("native_cop", "native", 0x24),
    ("native_brk", "native", 0x26),
    ("native_abort", "native", 0x28),
    ("native_nmi", "native", 0x2A),
    ("native_irq", "native", 0x2E),
    ("emulation_cop", "emulation", 0x34),
    ("emulation_abort", "emulation", 0x38),
    ("emulation_nmi", "emulation", 0x3A),
    ("emulation_reset", "emulation", 0x3C),
    ("emulation_irq_brk", "emulation", 0x3E),
)


def _mapping_name(header_offset: int, map_mode: int) -> str:
    # KSS is an SA-1 LoROM cartridge (mode $23). Rejecting unknown layouts is
    # safer than emitting plausible-but-wrong CPU addresses.
    if header_offset == 0x7FC0 and map_mode == 0x23:
        return "sa1-lorom"
    if header_offset == 0x7FC0:
        return "lorom"
    if header_offset in (0xFFC0, 0x40FFC0):
        return "hirom"
    raise RomFormatError(f"unsupported internal-header offset 0x{header_offset:X}")


def _lorom_offset(cpu_address: int, payload_size: int) -> int | None:
    bank = (cpu_address >> 16) & 0xFF
    address = cpu_address & 0xFFFF
    if address < 0x8000:
        # A zero/low vector is legal metadata even when the cartridge does not
        # install that handler.  It has no corresponding ROM payload offset.
        return None
    return (((bank & 0x7F) << 15) | (address & 0x7FFF)) % payload_size


def observed_processor_entry(trace: dict[str, Any], processor: str) -> int:
    """Return a processor's first PC by its cycle from sanitized trace metadata."""
    if processor not in ("scpu", "sa1"):
        raise ValueError(f"unsupported processor {processor!r}")
    candidates = [
        block for block in trace.get("blocks", [])
        if block.get("processor") == processor
        and isinstance(block.get("pc"), int)
        and isinstance(block.get("first_cycle"), int)
    ]
    if not candidates:
        label = "SA-1" if processor == "sa1" else "S-CPU"
        raise RomFormatError(f"sanitized trace contains no {label} execution blocks")
    first = min(candidates, key=lambda block: (block["first_cycle"], block["pc"]))
    pc = first["pc"]
    if not 0 <= pc <= 0xFFFFFF:
        raise RomFormatError("sanitized trace contains an out-of-range SA-1 PC")
    return pc


def observed_sa1_reset(trace: dict[str, Any]) -> int:
    return observed_processor_entry(trace, "sa1")


def extract_vectors(data: bytes, *, sa1_reset_pc: int | None = None) -> dict[str, Any]:
    inspection = inspect(data)
    payload, _ = _strip_copier_header(data)
    header = inspection.header
    mapping = _mapping_name(header.file_offset, header.map_mode)
    if mapping not in ("sa1-lorom", "lorom"):
        raise RomFormatError("vector extraction currently requires a LoROM address map")

    vectors = []
    for name, mode, relative_offset in _VECTOR_FIELDS:
        target16 = int.from_bytes(
            payload[header.file_offset + relative_offset : header.file_offset + relative_offset + 2],
            "little",
        )
        cpu_address = target16  # 65C816 hardware vectors load bank $00.
        vectors.append({
            "name": name,
            "mode": mode,
            "vector_cpu_address": 0x00FFE4 + relative_offset - 0x24,
            "target_cpu_address": cpu_address,
            "target_rom_offset": _lorom_offset(cpu_address, len(payload)),
        })

    sa1: dict[str, Any] = {
        "vector_source": "runtime-control-registers",
        "control_registers": {
            "reset_low": 0x2203,
            "reset_high": 0x2204,
            "nmi_low": 0x2205,
            "nmi_high": 0x2206,
            "irq_low": 0x2207,
            "irq_high": 0x2208,
        },
    }
    if sa1_reset_pc is not None:
        if not 0 <= sa1_reset_pc <= 0xFFFFFF:
            raise RomFormatError("SA-1 reset PC is outside the 24-bit address space")
        sa1["observed_reset_entry"] = {
            "target_cpu_address": sa1_reset_pc,
            "target_rom_offset": _lorom_offset(sa1_reset_pc, len(payload)),
            "basis": "first-sa1-execution-event",
        }

    return {
        "schema_version": 1,
        "cartridge": {
            "payload_size": len(payload),
            "copier_header": inspection.identity.copier_header,
            "internal_header_offset": header.file_offset,
            "map_mode": header.map_mode,
            "mapping": mapping,
        },
        "processors": {
            "scpu": {"vector_source": "rom-hardware-vector-table", "vectors": vectors},
            "sa1": sa1,
        },
    }


def load_observed_sa1_reset(path: Path) -> int:
    return observed_sa1_reset(json.loads(path.read_text(encoding="utf-8")))


def load_observed_processor_entry(path: Path, processor: str) -> int:
    return observed_processor_entry(json.loads(path.read_text(encoding="utf-8")), processor)
