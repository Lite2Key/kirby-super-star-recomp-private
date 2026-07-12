"""Stable identities shared by discovery, lifting, and validation."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class Processor(str, Enum):
    SCPU = "scpu"
    SA1 = "sa1"


@dataclass(frozen=True, order=True)
class CpuMode:
    """65C816 execution mode at a specific instruction boundary.

    ``m8`` and ``x8`` are explicit rather than inferred. In emulation mode the
    hardware forces both width flags to 8-bit, which is enforced here.
    """

    emulation: bool
    m8: bool
    x8: bool

    def __post_init__(self) -> None:
        if self.emulation and not (self.m8 and self.x8):
            raise ValueError("emulation mode requires 8-bit accumulator and index registers")

    @classmethod
    def reset(cls) -> "CpuMode":
        return cls(emulation=True, m8=True, x8=True)

    @property
    def key(self) -> str:
        return f"e{int(self.emulation)}m{int(self.m8)}x{int(self.x8)}"


@dataclass(frozen=True, order=True)
class BlockIdentity:
    processor: Processor
    pc: int
    mode: CpuMode

    def __post_init__(self) -> None:
        if not 0 <= self.pc <= 0xFFFFFF:
            raise ValueError("PC must be a 24-bit address")

    @property
    def symbol(self) -> str:
        return f"block_{self.processor.value}_{self.pc:06x}_{self.mode.key}"

    def to_dict(self) -> dict[str, object]:
        return {
            "processor": self.processor.value,
            "pc": self.pc,
            "mode": {
                "emulation": self.mode.emulation,
                "m8": self.mode.m8,
                "x8": self.mode.x8,
            },
        }
