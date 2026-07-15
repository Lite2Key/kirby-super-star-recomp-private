"""Extract a Mesen screenshot into an ignored private path.

The raw PNG contains copyrighted game pixels and must never be committed.
"""

from __future__ import annotations

import argparse
import binascii
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import zlib

from .importer import TraceFormatError


ARGB_START = re.compile(r"KSS_PPU_VISIBLE_ARGB_START_V1\|(\d+)\|(\d+)")
ARGB_CHUNK = re.compile(r"KSS_PPU_VISIBLE_ARGB_CHUNK_V1\|(\d+)\|([0-9A-Fa-f]+)")
ARGB_END = re.compile(r"KSS_PPU_VISIBLE_ARGB_END_V1\|(\d+)\|(\d+)")
VISIBLE = re.compile(
    r"KSS_PPU_VISIBLE_V1\|visible\|(\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)"
)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_PNG_BYTES = 4 * 1024 * 1024
MAX_CHUNKS = 4096
MAX_PIXELS = 1024 * 1024


@dataclass(frozen=True)
class VisibleFrameMetadata:
    frame: int
    width: int
    height: int
    pixel_count: int
    nonblack_pixels: int
    master_clock: int
    scpu_cycle: int
    png_bytes: int


def _validate_png(data: bytes) -> tuple[int, int]:
    if not data.startswith(PNG_SIGNATURE):
        raise TraceFormatError("screenshot does not have a PNG signature")
    offset = len(PNG_SIGNATURE)
    dimensions: tuple[int, int] | None = None
    saw_iend = False
    while offset < len(data):
        if len(data) - offset < 12:
            raise TraceFormatError("truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise TraceFormatError("PNG chunk exceeds declared image size")
        payload = data[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        observed_crc = binascii.crc32(kind + payload) & 0xFFFFFFFF
        if observed_crc != expected_crc:
            raise TraceFormatError(f"PNG {kind!r} chunk has an invalid CRC")
        if kind == b"IHDR":
            if dimensions is not None or length != 13:
                raise TraceFormatError("PNG must have one valid IHDR chunk")
            dimensions = struct.unpack_from(">II", payload, 0)
        if kind == b"IEND":
            if length != 0 or end != len(data):
                raise TraceFormatError("PNG IEND must terminate the image")
            saw_iend = True
        offset = end
    if dimensions is None or not saw_iend:
        raise TraceFormatError("PNG is missing IHDR or IEND")
    return dimensions


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)
    )


def _rgba_scanlines(argb: list[int], width: int, height: int) -> bytes:
    """Convert Mesen's row-major ARGB buffer to opaque RGBA PNG rows."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: none
        for value in argb[y * width : (y + 1) * width]:
            rows.extend(((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 0xFF))
    return bytes(rows)


def _encode_png(argb: list[int], width: int, height: int) -> bytes:
    rows = _rgba_scanlines(argb, width, height)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        PNG_SIGNATURE
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", zlib.compress(rows, level=9))
        + _png_chunk(b"IEND", b"")
    )


def extract_visible_png(lines: list[str]) -> tuple[bytes, VisibleFrameMetadata]:
    declared_pixels: int | None = None
    declared_chunks: int | None = None
    chunks: dict[int, list[int]] = {}
    end: tuple[int, int] | None = None
    visible: tuple[int, ...] | None = None
    for line in lines:
        if match := ARGB_START.search(line):
            if declared_pixels is not None:
                raise TraceFormatError("multiple framebuffer start markers")
            declared_pixels, declared_chunks = map(int, match.groups())
            if not 1 <= declared_pixels <= MAX_PIXELS:
                raise TraceFormatError("framebuffer pixel count is outside the safety range")
            if not 1 <= declared_chunks <= MAX_CHUNKS:
                raise TraceFormatError("framebuffer chunk count is outside the safety range")
            continue
        if match := ARGB_CHUNK.search(line):
            if declared_pixels is None or end is not None:
                raise TraceFormatError("framebuffer chunk lies outside its markers")
            index = int(match.group(1))
            if index in chunks:
                raise TraceFormatError("duplicate framebuffer chunk")
            encoded = match.group(2)
            if len(encoded) % 8:
                raise TraceFormatError("invalid framebuffer chunk hex")
            chunks[index] = [int(encoded[offset : offset + 8], 16) for offset in range(0, len(encoded), 8)]
            continue
        if match := ARGB_END.search(line):
            if end is not None:
                raise TraceFormatError("multiple framebuffer end markers")
            end = tuple(map(int, match.groups()))
            continue
        if match := VISIBLE.search(line):
            if visible is not None:
                raise TraceFormatError("multiple visible-frame markers")
            visible = tuple(map(int, match.groups()))

    if declared_pixels is None or declared_chunks is None or end is None:
        raise TraceFormatError("incomplete framebuffer marker sequence")
    if end != (declared_chunks, declared_pixels):
        raise TraceFormatError("framebuffer end marker disagrees with its start")
    if set(chunks) != set(range(1, declared_chunks + 1)):
        raise TraceFormatError("framebuffer chunks are not contiguous")
    argb = [value for index in range(1, declared_chunks + 1) for value in chunks[index]]
    if len(argb) != declared_pixels:
        raise TraceFormatError("framebuffer payload length disagrees with its marker")
    if visible is None:
        raise TraceFormatError("missing visible-frame marker")
    frame, width, height, pixel_count, nonblack, master, scpu = visible
    if pixel_count != width * height or pixel_count != declared_pixels:
        raise TraceFormatError("visible-frame dimensions disagree with the framebuffer")
    if nonblack != sum(value != 0 for value in argb) or not 1 <= nonblack <= pixel_count:
        raise TraceFormatError("visible-frame pixel counts are inconsistent")
    data = _encode_png(argb, width, height)
    if len(data) > MAX_PNG_BYTES or _validate_png(data) != (width, height):
        raise TraceFormatError("encoded PNG failed validation")
    return data, VisibleFrameMetadata(
        frame, width, height, pixel_count, nonblack, master, scpu, len(data)
    )


def _require_private_output(path: Path) -> None:
    if ".private" not in path.resolve().parts:
        raise TraceFormatError("raw screenshot output must be inside a .private directory")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="private raw Mesen log")
    parser.add_argument("output", type=Path, help="private PNG output")
    args = parser.parse_args(argv)
    try:
        _require_private_output(args.output)
        png, metadata = extract_visible_png(
            args.input.read_text(encoding="utf-8", errors="replace").splitlines()
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(png)
    except (OSError, TraceFormatError) as error:
        parser.error(str(error))
    print(
        f"visible frame {metadata.frame}: {metadata.width}x{metadata.height}, "
        f"{metadata.nonblack_pixels} nonblack pixels, {metadata.png_bytes} PNG bytes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
