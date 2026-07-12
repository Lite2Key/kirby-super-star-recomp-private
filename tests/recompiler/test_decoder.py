import pytest

from recompiler.kssrecomp.decoder import (
    OPCODES,
    AddressingMode,
    DecoderState,
    Flow,
    decode_one,
    metadata,
    operand_width,
)
from recompiler.kssrecomp.errors import AmbiguousModeError, DecodeError
from recompiler.kssrecomp.model import CpuMode


def test_reset_path_enters_native_and_changes_widths() -> None:
    state = DecoderState(CpuMode.reset())
    clc = decode_one(bytes([0x18]), 0x008000, state)
    xce = decode_one(bytes([0xFB]), 0x008001, clc.state_after)
    rep = decode_one(bytes([0xC2, 0x30]), 0x008002, xce.state_after)
    assert xce.state_after.mode == CpuMode(False, True, True)
    assert xce.state_after.carry is True
    assert rep.state_after.mode == CpuMode(False, False, False)


def test_immediate_width_follows_mode() -> None:
    narrow = decode_one(bytes([0xA9, 0x12]), 0x808000, DecoderState(CpuMode.reset()))
    wide = decode_one(bytes([0xA9, 0x34, 0x12]), 0x808000, DecoderState(CpuMode(False, False, True)))
    assert narrow.size == 2
    assert wide.size == 3
    assert wide.operand == bytes([0x34, 0x12])


def test_relative_branch_stays_in_program_bank() -> None:
    insn = decode_one(bytes([0xD0, 0xFC]), 0x80FFFE, DecoderState(CpuMode.reset()))
    assert insn.flow == Flow.BRANCH
    assert insn.target == 0x80FFFC


def test_indirect_jump_requires_dynamic_discovery() -> None:
    insn = decode_one(bytes([0x6C, 0x00, 0x20]), 0x008100, DecoderState(CpuMode.reset()))
    assert insn.flow == Flow.JUMP
    assert insn.target is None


def test_xce_fails_when_carry_is_unknown() -> None:
    with pytest.raises(AmbiguousModeError, match="known carry"):
        decode_one(bytes([0xFB]), 0x008000, DecoderState(CpuMode.reset()))


def test_truncated_instruction_fails_hard() -> None:
    with pytest.raises(DecodeError, match="truncated"):
        decode_one(bytes([0xA9]), 0, DecoderState(CpuMode.reset()))


def test_all_256_opcodes_have_deterministic_metadata_and_decode() -> None:
    assert len(OPCODES) == 256
    state = DecoderState(CpuMode(False, False, False), carry=False)
    for opcode in range(256):
        meta = metadata(opcode)
        width = operand_width(meta.addressing, state.mode)
        encoded = bytes([opcode, 0, 0, 0])
        first = decode_one(encoded, 0x808000, state, restored_state=state)
        second = decode_one(encoded, 0x808000, state, restored_state=state)
        assert meta.opcode == opcode
        assert first == second
        assert first.size == width + 1
        assert 1 <= first.size <= 4


def test_addressing_mode_sizes_cover_65c816_special_cases() -> None:
    native_wide = CpuMode(False, False, False)
    native_narrow = CpuMode(False, True, True)
    assert operand_width(AddressingMode.IMPLIED, native_wide) == 0
    assert operand_width(AddressingMode.IMMEDIATE_M, native_wide) == 2
    assert operand_width(AddressingMode.IMMEDIATE_M, native_narrow) == 1
    assert operand_width(AddressingMode.IMMEDIATE_X, native_wide) == 2
    assert operand_width(AddressingMode.BLOCK_MOVE, native_wide) == 2
    assert operand_width(AddressingMode.ABSOLUTE_LONG, native_wide) == 3
    assert operand_width(AddressingMode.STACK_DIRECT_INDIRECT, native_wide) == 1
    assert operand_width(AddressingMode.STACK_PC_RELATIVE, native_wide) == 2


@pytest.mark.parametrize(
    ("opcode", "mnemonic", "addressing", "flow"),
    [
        (0x00, "BRK", AddressingMode.IMMEDIATE8, Flow.INTERRUPT),
        (0x22, "JSL", AddressingMode.ABSOLUTE_LONG, Flow.CALL),
        (0x44, "MVP", AddressingMode.BLOCK_MOVE, Flow.NEXT),
        (0x6C, "JMP", AddressingMode.ABSOLUTE_INDIRECT, Flow.JUMP),
        (0x82, "BRL", AddressingMode.RELATIVE16, Flow.BRANCH),
        (0xA9, "LDA", AddressingMode.IMMEDIATE_M, Flow.NEXT),
        (0xDC, "JML", AddressingMode.ABSOLUTE_INDIRECT_LONG, Flow.JUMP),
        (0xFC, "JSR", AddressingMode.ABSOLUTE_INDEXED_INDIRECT_X, Flow.CALL),
    ],
)
def test_representative_metadata(opcode, mnemonic, addressing, flow) -> None:
    item = metadata(opcode)
    assert (item.mnemonic, item.addressing, item.flow) == (mnemonic, addressing, flow)


def test_metadata_rejects_non_byte_opcode() -> None:
    with pytest.raises(ValueError, match="unsigned byte"):
        metadata(256)


def test_status_restore_requires_observed_mode_and_carry() -> None:
    state = DecoderState(CpuMode(False, False, False), carry=None)
    with pytest.raises(AmbiguousModeError, match="PLP requires observed"):
        decode_one(bytes([0x28]), 0x808000, state)
    restored = DecoderState(CpuMode(False, True, False), carry=True)
    assert decode_one(bytes([0x28]), 0x808000, state, restored_state=restored).state_after == restored


def test_carry_fact_is_invalidated_by_arithmetic_before_xce() -> None:
    state = DecoderState(CpuMode(False, True, True), carry=False)
    after_adc = decode_one(bytes([0x69, 0]), 0x808000, state).state_after
    assert after_adc.carry is None
    with pytest.raises(AmbiguousModeError, match="known carry"):
        decode_one(bytes([0xFB]), 0x808002, after_adc)


def test_rep_and_sep_track_carry_bit() -> None:
    state = DecoderState(CpuMode(False, True, True), carry=None)
    assert decode_one(bytes([0xC2, 0x01]), 0, state).state_after.carry is False
    assert decode_one(bytes([0xE2, 0x01]), 0, state).state_after.carry is True
