from __future__ import annotations

from pathlib import Path

import pytest

from kss_trace.importer import TraceFormatError
from kss_trace.visible_frame_capture import _rgba_scanlines, extract_visible_png, main


def _lines() -> list[str]:
    # Mesen is row-major: red/green are the top row, blue/black the bottom.
    pieces = ([0xFFFF0000, 0xFF00FF00], [0xFF0000FF, 0])
    return [
        "KSS_PPU_VISIBLE_ARGB_START_V1|4|2",
        "KSS_PPU_VISIBLE_ARGB_CHUNK_V1|1|" + "".join(f"{value:08X}" for value in pieces[0]),
        "KSS_PPU_VISIBLE_ARGB_CHUNK_V1|2|" + "".join(f"{value:08X}" for value in pieces[1]),
        "KSS_PPU_VISIBLE_ARGB_END_V1|2|4",
        "KSS_PPU_VISIBLE_V1|visible|7|2|2|4|3|1234|234",
    ]


def test_preserves_mesen_row_major_pixel_order_in_png_rows() -> None:
    rows = _rgba_scanlines([0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0], 2, 2)
    assert rows == bytes((
        0, 255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 0, 255, 255, 0, 0, 0, 255,
    ))


def test_extracts_complete_validated_png() -> None:
    observed, metadata = extract_visible_png(_lines())
    assert observed.startswith(b"\x89PNG\r\n\x1a\n")
    assert metadata.frame == 7
    assert metadata.nonblack_pixels == 3


@pytest.mark.parametrize(
    ("mutate", "message"),
    [
        (lambda lines: [line for line in lines if "CHUNK_V1|2" not in line], "not contiguous"),
        (lambda lines: [*lines[:3], lines[1], *lines[3:]], "duplicate"),
        (lambda lines: [line.replace("|2|2|4|3|", "|3|2|6|3|") for line in lines], "dimensions disagree"),
    ],
)
def test_rejects_incomplete_or_inconsistent_capture(mutate, message: str) -> None:
    with pytest.raises(TraceFormatError, match=message):
        extract_visible_png(mutate(_lines()))


def test_cli_refuses_output_outside_private_directory(tmp_path: Path) -> None:
    log = tmp_path / "capture.log"
    log.write_text("\n".join(_lines()), encoding="utf-8")
    with pytest.raises(SystemExit):
        main([str(log), str(tmp_path / "frame.png")])


def test_cli_writes_inside_private_directory(tmp_path: Path) -> None:
    log = tmp_path / "capture.log"
    log.write_text("\n".join(_lines()), encoding="utf-8")
    output = tmp_path / ".private" / "frame.png"
    assert main([str(log), str(output)]) == 0
    assert output.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
