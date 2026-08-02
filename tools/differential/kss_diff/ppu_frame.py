"""Sanitize the private forced-blank first-frame screen-buffer oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def summarize_ppu_frame(path: Path) -> dict[str, object]:
    raw = path.read_bytes()
    markers = [line for line in raw.decode("utf-8", errors="strict").splitlines()
               if line.startswith("KSS_PPU_FRAME_V1|")]
    if len(markers) != 1:
        raise ValueError("PPU oracle requires exactly one frame marker")
    parts = markers[0].split("|")
    if len(parts) != 10:
        raise ValueError("malformed PPU frame marker")
    frame, width, height, count, first, nonblack = map(int, parts[1:7])
    uniform, forced = parts[7:9]
    brightness = int(parts[9])
    if frame != 1 or width <= 0 or height <= 0 or count != width * height:
        raise ValueError("invalid first-frame dimensions")
    if (uniform, forced, first, nonblack) != ("true", "true", 0, 0):
        raise ValueError("first-frame forced-blank proof is not uniformly black")
    rgba = bytes((0, 0, 0, 255)) * count
    return {
        "schema_version": 1,
        "source_format": "kss-mesen-screen-buffer-v1",
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "boundary": "first_end_frame",
        "frame_number": frame,
        "width": width,
        "height": height,
        "pixel_format": "rgba8888",
        "forced_blank": True,
        "brightness": brightness,
        "uniform": True,
        "nonblack_pixels": nonblack,
        "rgba_sha256": hashlib.sha256(rgba).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_ppu_frame(args.input), indent=2) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing PPU frame summary: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
