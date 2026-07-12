import hashlib

import pytest

from recompiler.kssrecomp.errors import RomFormatError
from recompiler.kssrecomp.rom import identify, inspect


def synthetic_lorom() -> bytes:
    rom = bytearray(0x8000)
    base = 0x7FC0
    rom[base : base + 21] = b"SYNTHETIC TEST ROM".ljust(21, b" ")
    rom[base + 0x15] = 0x23
    rom[base + 0x16] = 0x35
    rom[base + 0x17] = 0x09
    rom[base + 0x18] = 0x03
    rom[base + 0x19] = 0x01
    rom[base + 0x1B] = 0
    rom[base + 0x1C : base + 0x1E] = (0xEDCB).to_bytes(2, "little")
    rom[base + 0x1E : base + 0x20] = (0x1234).to_bytes(2, "little")
    rom[base + 0x3C : base + 0x3E] = (0x8123).to_bytes(2, "little")
    return bytes(rom)


def test_identify_hashes_payload_without_exporting_it() -> None:
    rom = synthetic_lorom()
    result = identify(rom)
    assert result.payload_size == 0x8000
    assert result.sha256 == hashlib.sha256(rom).hexdigest().upper()
    assert not result.copier_header


def test_copier_header_is_excluded_from_identity() -> None:
    rom = synthetic_lorom()
    result = identify(bytes(512) + rom)
    assert result.copier_header
    assert result.payload_size == len(rom)
    assert result.sha256 == hashlib.sha256(rom).hexdigest().upper()


def test_inspect_internal_header_and_reset_vector() -> None:
    result = inspect(synthetic_lorom())
    assert result.header.title == "SYNTHETIC TEST ROM"
    assert result.header.map_mode == 0x23
    assert result.header.cartridge_type == 0x35
    assert result.header.checksum_pair_valid
    assert result.header.reset_vector == 0x8123
    assert "identity" in result.to_dict()


def test_rejects_misaligned_payload() -> None:
    with pytest.raises(RomFormatError, match="32 KiB"):
        identify(bytes(123))
