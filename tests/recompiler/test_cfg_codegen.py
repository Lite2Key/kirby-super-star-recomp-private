from recompiler.kssrecomp.cfg import BasicBlock, ControlFlowGraph, Edge, EdgeKind
from recompiler.kssrecomp.codegen import emit_cpp
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
    assert "Semantics not lifted in v1" in cpp
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
