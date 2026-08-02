from __future__ import annotations

import hashlib
import json
from pathlib import Path

import jsonschema

from differential.kss_diff.ppu_frame import summarize_ppu_frame


ROOT = Path(__file__).resolve().parents[2]


def test_first_frame_reference_is_value_free_schema_valid_and_reproducible() -> None:
    reference = json.loads(
        (ROOT / "analysis/differential/ppu-first-frame-reference.json").read_text()
    )
    schema = json.loads(
        (ROOT / "schemas/differential/ppu-first-frame.schema.json").read_text()
    )
    jsonschema.validate(reference, schema)
    rgba = bytes((0, 0, 0, 255)) * (reference["width"] * reference["height"])
    assert hashlib.sha256(rgba).hexdigest() == reference["rgba_sha256"]
    assert set(reference).isdisjoint({"pixels", "image", "screenshot", "rom_bytes"})


def test_renderer_is_explicitly_bounded_to_forced_blank_or_mode1_bg1() -> None:
    header = (ROOT / "include/kss/snes_frame_renderer.hpp").read_text()
    assert "unsupported_visible_mode" in header
    assert "if (state.forced_blank)" in header
    assert "unsupported_feature" in header
    assert "tilemap_base" in header and "character_base" in header
    assert "return render(registers.ppu_state())" in header
    assert "kSnesFrameWidth = 256" in header
    assert "kSnesFrameHeight = 239" in header


def test_oracle_sanitizer_rejects_nonblank_and_keeps_pixels_private(tmp_path: Path) -> None:
    source = tmp_path / "ppu.log"
    source.write_text("KSS_PPU_FRAME_V1|1|2|1|2|0|0|true|true|15\n")
    summary = summarize_ppu_frame(source)
    assert summary["width"] == 2 and summary["height"] == 1
    assert set(summary).isdisjoint({"pixels", "image", "screenshot"})
    source.write_text("KSS_PPU_FRAME_V1|1|2|1|2|1|1|false|false|15\n")
    import pytest
    with pytest.raises(ValueError, match="forced-blank"):
        summarize_ppu_frame(source)
