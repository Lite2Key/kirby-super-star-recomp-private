"""Serializable control-flow graph records with explicit discovery evidence."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum

from .decoder import Instruction
from .model import BlockIdentity


class EdgeKind(str, Enum):
    FALLTHROUGH = "fallthrough"
    BRANCH_TAKEN = "branch_taken"
    CALL = "call"
    JUMP = "jump"
    DYNAMIC = "dynamic"


@dataclass(frozen=True)
class Edge:
    kind: EdgeKind
    source: BlockIdentity
    target: BlockIdentity | None
    evidence_id: str | None = None

    def to_dict(self) -> dict[str, object]:
        return {
            "kind": self.kind.value,
            "source": self.source.to_dict(),
            "target": None if self.target is None else self.target.to_dict(),
            "evidence_id": self.evidence_id,
        }


@dataclass
class BasicBlock:
    identity: BlockIdentity
    instructions: list[Instruction] = field(default_factory=list)
    edges: list[Edge] = field(default_factory=list)

    def to_dict(self) -> dict[str, object]:
        return {
            "identity": self.identity.to_dict(),
            "instructions": [
                {
                    "pc": item.pc,
                    "opcode": item.opcode,
                    "mnemonic": item.mnemonic,
                    "addressing": item.addressing.value,
                    "operand_hex": item.operand.hex().upper(),
                    "size": item.size,
                    "flow": item.flow.value,
                    "target": item.target,
                    "mode_after": item.state_after.mode.key,
                }
                for item in self.instructions
            ],
            "edges": [edge.to_dict() for edge in self.edges],
        }


@dataclass
class ControlFlowGraph:
    blocks: dict[BlockIdentity, BasicBlock] = field(default_factory=dict)

    def add(self, block: BasicBlock) -> None:
        if block.identity in self.blocks:
            raise ValueError(f"duplicate block: {block.identity.symbol}")
        self.blocks[block.identity] = block

    def to_dict(self) -> dict[str, object]:
        ordered = sorted(
            self.blocks.values(),
            key=lambda block: (block.identity.processor.value, block.identity.pc, block.identity.mode.key),
        )
        return {"schema_version": 1, "blocks": [block.to_dict() for block in ordered]}
