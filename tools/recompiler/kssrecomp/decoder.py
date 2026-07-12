"""Strict, complete, mode-aware 65C816 instruction decoding metadata."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum

from .errors import AmbiguousModeError, DecodeError
from .model import CpuMode


class Flow(str, Enum):
    NEXT = "next"
    BRANCH = "branch"
    JUMP = "jump"
    CALL = "call"
    RETURN = "return"
    INTERRUPT = "interrupt"
    INTERRUPT_RETURN = "interrupt_return"
    STOP = "stop"


class AddressingMode(str, Enum):
    IMPLIED = "implied"
    ACCUMULATOR = "accumulator"
    IMMEDIATE8 = "immediate8"
    IMMEDIATE_M = "immediate_m"
    IMMEDIATE_X = "immediate_x"
    DIRECT = "direct"
    DIRECT_X = "direct_x"
    DIRECT_Y = "direct_y"
    DIRECT_INDIRECT = "direct_indirect"
    DIRECT_INDIRECT_LONG = "direct_indirect_long"
    DIRECT_INDEXED_INDIRECT_X = "direct_indexed_indirect_x"
    DIRECT_INDIRECT_INDEXED_Y = "direct_indirect_indexed_y"
    DIRECT_INDIRECT_LONG_INDEXED_Y = "direct_indirect_long_indexed_y"
    STACK_RELATIVE = "stack_relative"
    STACK_RELATIVE_INDIRECT_INDEXED_Y = "stack_relative_indirect_indexed_y"
    STACK_ABSOLUTE = "stack_absolute"
    STACK_DIRECT_INDIRECT = "stack_direct_indirect"
    STACK_PC_RELATIVE = "stack_pc_relative"
    ABSOLUTE = "absolute"
    ABSOLUTE_X = "absolute_x"
    ABSOLUTE_Y = "absolute_y"
    ABSOLUTE_LONG = "absolute_long"
    ABSOLUTE_LONG_X = "absolute_long_x"
    ABSOLUTE_INDIRECT = "absolute_indirect"
    ABSOLUTE_INDEXED_INDIRECT_X = "absolute_indexed_indirect_x"
    ABSOLUTE_INDIRECT_LONG = "absolute_indirect_long"
    RELATIVE8 = "relative8"
    RELATIVE16 = "relative16"
    BLOCK_MOVE = "block_move"


@dataclass(frozen=True)
class OpcodeMetadata:
    opcode: int
    mnemonic: str
    addressing: AddressingMode
    flow: Flow = Flow.NEXT


@dataclass(frozen=True)
class DecoderState:
    mode: CpuMode
    carry: bool | None = None


@dataclass(frozen=True)
class Instruction:
    pc: int
    opcode: int
    mnemonic: str
    addressing: AddressingMode
    operand: bytes
    size: int
    flow: Flow
    target: int | None
    state_after: DecoderState

    @property
    def bytes_(self) -> bytes:
        return bytes((self.opcode,)) + self.operand


I = AddressingMode.IMPLIED
A = AddressingMode.ACCUMULATOR
N = AddressingMode.IMMEDIATE8
M = AddressingMode.IMMEDIATE_M
X = AddressingMode.IMMEDIATE_X
D = AddressingMode.DIRECT
DX = AddressingMode.DIRECT_X
DY = AddressingMode.DIRECT_Y
DI = AddressingMode.DIRECT_INDIRECT
DL = AddressingMode.DIRECT_INDIRECT_LONG
DIX = AddressingMode.DIRECT_INDEXED_INDIRECT_X
DIY = AddressingMode.DIRECT_INDIRECT_INDEXED_Y
DLY = AddressingMode.DIRECT_INDIRECT_LONG_INDEXED_Y
SR = AddressingMode.STACK_RELATIVE
SRIY = AddressingMode.STACK_RELATIVE_INDIRECT_INDEXED_Y
SABS = AddressingMode.STACK_ABSOLUTE
SDI = AddressingMode.STACK_DIRECT_INDIRECT
SPCR = AddressingMode.STACK_PC_RELATIVE
AB = AddressingMode.ABSOLUTE
ABX = AddressingMode.ABSOLUTE_X
ABY = AddressingMode.ABSOLUTE_Y
ABL = AddressingMode.ABSOLUTE_LONG
ALX = AddressingMode.ABSOLUTE_LONG_X
ABI = AddressingMode.ABSOLUTE_INDIRECT
ABIX = AddressingMode.ABSOLUTE_INDEXED_INDIRECT_X
ABIL = AddressingMode.ABSOLUTE_INDIRECT_LONG
R8 = AddressingMode.RELATIVE8
R16 = AddressingMode.RELATIVE16
BM = AddressingMode.BLOCK_MOVE


# Official WDC 65C816 opcode matrix, rows $0x through $Fx.
_MATRIX: tuple[tuple[tuple[str, AddressingMode], ...], ...] = (
    (("BRK",N),("ORA",DIX),("COP",N),("ORA",SR),("TSB",D),("ORA",D),("ASL",D),("ORA",DL),("PHP",I),("ORA",M),("ASL",A),("PHD",I),("TSB",AB),("ORA",AB),("ASL",AB),("ORA",ABL)),
    (("BPL",R8),("ORA",DIY),("ORA",DI),("ORA",SRIY),("TRB",D),("ORA",DX),("ASL",DX),("ORA",DLY),("CLC",I),("ORA",ABY),("INC",A),("TCS",I),("TRB",AB),("ORA",ABX),("ASL",ABX),("ORA",ALX)),
    (("JSR",AB),("AND",DIX),("JSL",ABL),("AND",SR),("BIT",D),("AND",D),("ROL",D),("AND",DL),("PLP",I),("AND",M),("ROL",A),("PLD",I),("BIT",AB),("AND",AB),("ROL",AB),("AND",ABL)),
    (("BMI",R8),("AND",DIY),("AND",DI),("AND",SRIY),("BIT",DX),("AND",DX),("ROL",DX),("AND",DLY),("SEC",I),("AND",ABY),("DEC",A),("TSC",I),("BIT",ABX),("AND",ABX),("ROL",ABX),("AND",ALX)),
    (("RTI",I),("EOR",DIX),("WDM",N),("EOR",SR),("MVP",BM),("EOR",D),("LSR",D),("EOR",DL),("PHA",I),("EOR",M),("LSR",A),("PHK",I),("JMP",AB),("EOR",AB),("LSR",AB),("EOR",ABL)),
    (("BVC",R8),("EOR",DIY),("EOR",DI),("EOR",SRIY),("MVN",BM),("EOR",DX),("LSR",DX),("EOR",DLY),("CLI",I),("EOR",ABY),("PHY",I),("TCD",I),("JML",ABL),("EOR",ABX),("LSR",ABX),("EOR",ALX)),
    (("RTS",I),("ADC",DIX),("PER",SPCR),("ADC",SR),("STZ",D),("ADC",D),("ROR",D),("ADC",DL),("PLA",I),("ADC",M),("ROR",A),("RTL",I),("JMP",ABI),("ADC",AB),("ROR",AB),("ADC",ABL)),
    (("BVS",R8),("ADC",DIY),("ADC",DI),("ADC",SRIY),("STZ",DX),("ADC",DX),("ROR",DX),("ADC",DLY),("SEI",I),("ADC",ABY),("PLY",I),("TDC",I),("JMP",ABIX),("ADC",ABX),("ROR",ABX),("ADC",ALX)),
    (("BRA",R8),("STA",DIX),("BRL",R16),("STA",SR),("STY",D),("STA",D),("STX",D),("STA",DL),("DEY",I),("BIT",M),("TXA",I),("PHB",I),("STY",AB),("STA",AB),("STX",AB),("STA",ABL)),
    (("BCC",R8),("STA",DIY),("STA",DI),("STA",SRIY),("STY",DX),("STA",DX),("STX",DY),("STA",DLY),("TYA",I),("STA",ABY),("TXS",I),("TXY",I),("STZ",AB),("STA",ABX),("STZ",ABX),("STA",ALX)),
    (("LDY",X),("LDA",DIX),("LDX",X),("LDA",SR),("LDY",D),("LDA",D),("LDX",D),("LDA",DL),("TAY",I),("LDA",M),("TAX",I),("PLB",I),("LDY",AB),("LDA",AB),("LDX",AB),("LDA",ABL)),
    (("BCS",R8),("LDA",DIY),("LDA",DI),("LDA",SRIY),("LDY",DX),("LDA",DX),("LDX",DY),("LDA",DLY),("CLV",I),("LDA",ABY),("TSX",I),("TYX",I),("LDY",ABX),("LDA",ABX),("LDX",ABY),("LDA",ALX)),
    (("CPY",X),("CMP",DIX),("REP",N),("CMP",SR),("CPY",D),("CMP",D),("DEC",D),("CMP",DL),("INY",I),("CMP",M),("DEX",I),("WAI",I),("CPY",AB),("CMP",AB),("DEC",AB),("CMP",ABL)),
    (("BNE",R8),("CMP",DIY),("CMP",DI),("CMP",SRIY),("PEI",SDI),("CMP",DX),("DEC",DX),("CMP",DLY),("CLD",I),("CMP",ABY),("PHX",I),("STP",I),("JML",ABIL),("CMP",ABX),("DEC",ABX),("CMP",ALX)),
    (("CPX",X),("SBC",DIX),("SEP",N),("SBC",SR),("CPX",D),("SBC",D),("INC",D),("SBC",DL),("INX",I),("SBC",M),("NOP",I),("XBA",I),("CPX",AB),("SBC",AB),("INC",AB),("SBC",ABL)),
    (("BEQ",R8),("SBC",DIY),("SBC",DI),("SBC",SRIY),("PEA",SABS),("SBC",DX),("INC",DX),("SBC",DLY),("SED",I),("SBC",ABY),("PLX",I),("XCE",I),("JSR",ABIX),("SBC",ABX),("INC",ABX),("SBC",ALX)),
)

_BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA", "BRL"}
_JUMPS = {0x4C, 0x5C, 0x6C, 0x7C, 0xDC}
_CALLS = {0x20, 0x22, 0xFC}
_RETURNS = {0x60, 0x6B}


def _flow(opcode: int, mnemonic: str) -> Flow:
    if mnemonic in _BRANCHES:
        return Flow.BRANCH
    if opcode in _JUMPS:
        return Flow.JUMP
    if opcode in _CALLS:
        return Flow.CALL
    if opcode in _RETURNS:
        return Flow.RETURN
    if opcode == 0x40:
        return Flow.INTERRUPT_RETURN
    if opcode in (0x00, 0x02):
        return Flow.INTERRUPT
    if opcode in (0xCB, 0xDB):
        return Flow.STOP
    return Flow.NEXT


OPCODES: tuple[OpcodeMetadata, ...] = tuple(
    OpcodeMetadata(row * 16 + column, mnemonic, addressing, _flow(row * 16 + column, mnemonic))
    for row, entries in enumerate(_MATRIX)
    for column, (mnemonic, addressing) in enumerate(entries)
)
assert len(OPCODES) == 256 and all(item.opcode == index for index, item in enumerate(OPCODES))


_ONE_BYTE_OPERAND = {
    N, D, DX, DY, DI, DL, DIX, DIY, DLY, SR, SRIY, SDI, R8,
}
_TWO_BYTE_OPERAND = {AB, ABX, ABY, ABI, ABIX, ABIL, SABS, SPCR, R16, BM}
_THREE_BYTE_OPERAND = {ABL, ALX}


def operand_width(addressing: AddressingMode, mode: CpuMode) -> int:
    if addressing in (I, A):
        return 0
    if addressing == M:
        return 1 if mode.m8 else 2
    if addressing == X:
        return 1 if mode.x8 else 2
    if addressing in _ONE_BYTE_OPERAND:
        return 1
    if addressing in _TWO_BYTE_OPERAND:
        return 2
    if addressing in _THREE_BYTE_OPERAND:
        return 3
    raise AssertionError(f"unclassified addressing mode: {addressing.value}")


def metadata(opcode: int) -> OpcodeMetadata:
    if not 0 <= opcode <= 0xFF:
        raise ValueError("opcode must be an unsigned byte")
    return OPCODES[opcode]


def _relative_target(pc: int, operand: bytes) -> int:
    displacement = int.from_bytes(operand, "little", signed=True)
    bank = pc & 0xFF0000
    low = (pc + 1 + len(operand) + displacement) & 0xFFFF
    return bank | low


def _absolute_target(pc: int, meta: OpcodeMetadata, operand: bytes) -> int | None:
    if meta.addressing in (ABI, ABIX, ABIL):
        return None
    value = int.from_bytes(operand, "little")
    return value if meta.addressing in (ABL, ALX) else (pc & 0xFF0000) | value


_CARRY_UNKNOWN = {"ADC", "SBC", "CMP", "CPX", "CPY", "ASL", "LSR", "ROL", "ROR"}


def _transition(
    opcode: int,
    mnemonic: str,
    operand: bytes,
    state: DecoderState,
    restored_state: DecoderState | None,
) -> DecoderState:
    mode = state.mode
    carry = state.carry
    if opcode in (0x28, 0x40):
        if restored_state is None:
            raise AmbiguousModeError(f"{mnemonic} requires observed restored processor state")
        if restored_state.mode.emulation != mode.emulation:
            raise AmbiguousModeError(f"{mnemonic} cannot change the emulation flag")
        return restored_state
    if mnemonic in _CARRY_UNKNOWN:
        carry = None
    if opcode == 0x18:
        carry = False
    elif opcode == 0x38:
        carry = True
    elif opcode in (0xC2, 0xE2):
        mask = operand[0]
        setting = opcode == 0xE2
        m8 = setting if mask & 0x20 else mode.m8
        x8 = setting if mask & 0x10 else mode.x8
        if mode.emulation:
            m8 = x8 = True
        mode = CpuMode(mode.emulation, m8, x8)
        if mask & 0x01:
            carry = setting
    elif opcode == 0xFB:
        if carry is None:
            raise AmbiguousModeError("XCE requires known carry state")
        old_emulation = mode.emulation
        mode = CpuMode(True, True, True) if carry else replace(mode, emulation=False)
        carry = old_emulation
    return DecoderState(mode=mode, carry=carry)


def decode_one(
    data: bytes,
    pc: int,
    state: DecoderState,
    *,
    restored_state: DecoderState | None = None,
) -> Instruction:
    if not 0 <= pc <= 0xFFFFFF:
        raise DecodeError("PC must be a 24-bit address")
    if not data:
        raise DecodeError(f"no instruction byte available at {pc:06X}")
    meta = OPCODES[data[0]]
    width = operand_width(meta.addressing, state.mode)
    if len(data) < width + 1:
        raise DecodeError(f"truncated {meta.mnemonic} at ${pc:06X}: need {width + 1} bytes")
    operand = data[1 : width + 1]
    target = None
    if meta.addressing in (R8, R16, SPCR):
        target = _relative_target(pc, operand)
    elif meta.flow in (Flow.JUMP, Flow.CALL):
        target = _absolute_target(pc, meta, operand)
    return Instruction(
        pc=pc, opcode=meta.opcode, mnemonic=meta.mnemonic, addressing=meta.addressing,
        operand=operand, size=width + 1, flow=meta.flow, target=target,
        state_after=_transition(meta.opcode, meta.mnemonic, operand, state, restored_state),
    )
