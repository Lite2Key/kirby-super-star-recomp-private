from recompiler.kssrecomp.cfg import BasicBlock, ControlFlowGraph, Edge, EdgeKind
from recompiler.kssrecomp.codegen import LIFTED_V1_OPCODES, emit_cpp
from recompiler.kssrecomp.decoder import DecoderState, decode_one
from recompiler.kssrecomp.model import BlockIdentity, CpuMode, Processor


def test_cfg_serialization_and_cpp_are_deterministic() -> None:
    mode = CpuMode.reset()
    later = BlockIdentity(Processor.SCPU, 0x008010, mode)
    first = BlockIdentity(Processor.SCPU, 0x008000, mode)
    graph = ControlFlowGraph()
    graph.add(BasicBlock(later, [decode_one(bytes([0xEA]), later.pc, DecoderState(mode))]))
    graph.add(BasicBlock(
        first,
        [decode_one(bytes([0x20, 0x10, 0x80]), first.pc, DecoderState(mode))],
        [Edge(EdgeKind.CALL, first, later, "synthetic-call")],
    ))
    payload = graph.to_dict()
    assert payload["blocks"][0]["identity"]["pc"] == first.pc
    cpp = emit_cpp(graph)
    assert cpp.index(first.symbol) < cpp.index(later.symbol)
    assert "$008000: 20 10 80" in cpp
    assert "LiftedInstruction{0x20" in cpp
    assert "Semantics not lifted in v1" not in cpp
    assert "LiftedInstruction{0xEA, {0x00, 0x00, 0x00}, 0}" in cpp
    assert "CpuContext& cpu, Bus& bus, Scheduler&" in cpp
    assert cpp == emit_cpp(graph)


def test_duplicate_block_is_rejected() -> None:
    identity = BlockIdentity(Processor.SA1, 0x008000, CpuMode.reset())
    graph = ControlFlowGraph()
    graph.add(BasicBlock(identity))
    try:
        graph.add(BasicBlock(identity))
    except ValueError as error:
        assert "duplicate block" in str(error)
    else:
        raise AssertionError("duplicate block was accepted")


def test_accumulator_alu_practical_modes_are_lifted() -> None:
    family_bases = (0x00, 0x20, 0x40, 0x60, 0xC0, 0xE0)
    mode_offsets = (0x05, 0x09, 0x0D, 0x0F, 0x15, 0x19, 0x1D, 0x1F)
    expected = {base | offset for base in family_bases for offset in mode_offsets}
    assert len(expected) == 48
    assert expected <= LIFTED_V1_OPCODES
    assert 0x01 in LIFTED_V1_OPCODES


def test_load_store_index_practical_modes_are_lifted() -> None:
    expected = {
        # LDA, LDX, LDY
        0xA5, 0xA9, 0xAD, 0xAF, 0xB5, 0xB9, 0xBD, 0xBF,
        0xA2, 0xA6, 0xAE, 0xB6, 0xBE,
        0xA0, 0xA4, 0xAC, 0xB4, 0xBC,
        # STA, STX, STY, STZ
        0x85, 0x8D, 0x8F, 0x95, 0x99, 0x9D, 0x9F,
        0x86, 0x8E, 0x96, 0x84, 0x8C, 0x94,
        0x64, 0x74, 0x9C, 0x9E,
    }
    assert len(expected) == 35
    assert expected <= LIFTED_V1_OPCODES
    assert 0xA3 in LIFTED_V1_OPCODES
    assert 0x83 in LIFTED_V1_OPCODES


def test_rmw_bit_compare_transfer_and_stack_opcodes_are_lifted() -> None:
    expected = {
        0x0A, 0x06, 0x0E, 0x16, 0x1E, 0x2A, 0x26, 0x2E, 0x36, 0x3E,
        0x4A, 0x46, 0x4E, 0x56, 0x5E, 0x6A, 0x66, 0x6E, 0x76, 0x7E,
        0x3A, 0xC6, 0xCE, 0xD6, 0xDE, 0x1A, 0xE6, 0xEE, 0xF6, 0xFE,
        0x89, 0x24, 0x2C, 0x34, 0x3C, 0x14, 0x1C, 0x04, 0x0C,
        0xE0, 0xE4, 0xEC, 0xC0, 0xC4, 0xCC,
        0xAA, 0xA8, 0xBA, 0x8A, 0x9A, 0x9B, 0x98, 0xBB,
        0x5B, 0x1B, 0x7B, 0x3B,
        0x08, 0x8B, 0xDA, 0x5A, 0x28, 0xFA, 0x7A, 0xD4, 0x62,
    }
    assert len(expected) == 66
    assert expected <= LIFTED_V1_OPCODES


def test_all_65c816_opcodes_are_whitelisted_exactly() -> None:
    assert LIFTED_V1_OPCODES == frozenset(range(0x100))
