import hashlib
import json
import tempfile
from pathlib import Path

import pytest

from recompiler.kssrecomp.private_build import (
    PrivateBuildError,
    build_outputs,
    write_outputs,
)
from recompiler.kssrecomp.wram_witness import parse_witness_lines


def _fixture():
    rom = bytearray(0x8000)
    rom[0:2] = b"\xEA\xEA"
    mode = {"emulation": True, "m8": True, "x8": True}
    blocks = [
        {"processor": "scpu", "pc": 0x008000, "mode": mode,
         "hits": 1, "first_cycle": 0, "last_cycle": 0},
        {"processor": "sa1", "pc": 0x008001, "mode": mode,
         "hits": 1, "first_cycle": 1, "last_cycle": 1},
    ]
    coverage = {
        "schema_version": 1,
        "source_format": "mesen-ce-kss-trace-v1",
        "capture": {"bounded": True, "limit": 2, "event_count": 2, "end_reason": "complete"},
        "processors": {
            "scpu": {"events": 1, "unique_blocks": 1},
            "sa1": {"events": 1, "unique_blocks": 1},
        },
        "blocks": blocks,
        "edges": [],
    }
    vectors = {
        "schema_version": 1,
        "cartridge": {
            "mapping": "sa1-lorom", "payload_size": len(rom), "copier_header": False,
        },
        "processors": {
            "scpu": {"vectors": [{"name": "emulation_reset", "target_cpu_address": 0x008000}]},
            "sa1": {"observed_reset_entry": {"target_cpu_address": 0x008001}},
        },
    }
    return bytes(rom), vectors, coverage


def test_private_build_is_deterministic_and_registers_both_processors():
    rom, vectors, coverage = _fixture()
    digest = hashlib.sha256(rom).hexdigest()
    first = build_outputs(rom, vectors, coverage, expected_sha256=digest)
    second = build_outputs(rom, vectors, coverage, expected_sha256=digest)
    assert first == second
    summary = json.loads(first["summary.json"])
    manifest = json.loads(first["manifest.json"])
    assert summary["decoded_blocks"] == 2
    assert summary["unresolved_blocks"] == 0
    assert manifest["registered_blocks"] == 2
    assert manifest["processors"] == {"sa1": 1, "scpu": 1}
    assert "0xEA" in first["src/private_first_frame_blocks.cpp"]
    frontier = json.loads(first["frontier.json"])
    assert frontier["generated_nodes"] == 2
    assert frontier["processors"]["scpu"]["new_nodes"] == 1


def test_private_build_rejects_wrong_revision_before_lifting():
    rom, vectors, coverage = _fixture()
    with pytest.raises(PrivateBuildError, match="SHA-256"):
        build_outputs(rom, vectors, coverage, expected_sha256="00" * 32)


def test_private_build_accepts_optional_wram_witness_and_omits_it_without_one():
    rom, vectors, coverage = _fixture()
    digest = hashlib.sha256(rom).hexdigest()
    without = build_outputs(rom, vectors, coverage, expected_sha256=digest)
    assert "wram-witness.json" not in without
    witness = parse_witness_lines(["KSS_WRAM_BYTES_V1|00000E|EA000000"])
    with_witness = build_outputs(
        rom, vectors, coverage, expected_sha256=digest, wram_witness=witness,
    )
    assert json.loads(with_witness["wram-witness.json"]) == witness


def test_private_writer_requires_private_root_and_supports_check():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        with pytest.raises(PrivateBuildError, match=".private"):
            write_outputs(root / "generated", {"a.txt": "a"})
        private = root / ".private" / "generated"
        write_outputs(private, {"nested/a.txt": "a"})
        write_outputs(private, {"nested/a.txt": "a"}, check=True)
        with pytest.raises(PrivateBuildError, match="stale"):
            write_outputs(private, {"nested/a.txt": "b"}, check=True)


def test_cmake_has_explicit_opt_in_private_generated_path():
    root = Path(__file__).resolve().parents[2]
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    boot = (root / "src/runtime/boot_probe.cpp").read_text(encoding="utf-8")
    assert "option(KSS_USE_PRIVATE_GENERATED" in cmake
    assert "private_first_frame_blocks.cpp" in cmake
    assert "register_private_first_frame_blocks" in boot
