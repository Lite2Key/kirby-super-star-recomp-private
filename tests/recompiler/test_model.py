import pytest

from recompiler.kssrecomp.model import BlockIdentity, CpuMode, Processor


def test_reset_mode_and_stable_symbol() -> None:
    identity = BlockIdentity(Processor.SCPU, 0x008000, CpuMode.reset())
    assert identity.symbol == "block_scpu_008000_e1m1x1"
    assert identity.to_dict()["pc"] == 0x008000


def test_emulation_mode_rejects_wide_registers() -> None:
    with pytest.raises(ValueError, match="emulation mode"):
        CpuMode(emulation=True, m8=False, x8=True)


def test_block_address_must_be_24_bit() -> None:
    with pytest.raises(ValueError, match="24-bit"):
        BlockIdentity(Processor.SA1, 0x1000000, CpuMode.reset())
