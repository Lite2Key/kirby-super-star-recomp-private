"""Sanitize an anchored first-end-frame-to-first-visible-frame trace."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Iterable

from .importer import EVENT, BlockKey, TraceFormatError, _parse_event, summarize_events


CONTRACT = "first-end-frame-to-first-visible-v1"
START = re.compile(r"KSS_POST_FRAME_START_V1\|(\d+)\|(\d+)")
ANCHOR = re.compile(
    r"KSS_POST_FRAME_ANCHOR_V1\|(scpu|sa1)\|(\d+)\|([0-9A-Fa-f]{6})\|([01])\|([01])\|([01])"
)
END = re.compile(
    r"KSS_POST_FRAME_END_V1\|(\d+)\|visible\|(\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)"
)
ABORT = re.compile(r"KSS_POST_FRAME_ABORT_V1\|([a-z_]+)")


def _anchor(match: re.Match[str]) -> tuple[BlockKey, int]:
    processor, cycle, pc, emulation, m8, x8 = match.groups()
    identity = BlockKey(
        processor=processor,
        pc=int(pc, 16),
        emulation=emulation == "1",
        m8=m8 == "1",
        x8=x8 == "1",
    )
    if identity.emulation and not (identity.m8 and identity.x8):
        raise TraceFormatError(f"{processor} anchor: emulation mode requires M=1 and X=1")
    return identity, int(cycle)


def import_post_frame_lines(lines: Iterable[str]) -> dict[str, object]:
    """Validate the window contract and return identity-only route coverage."""
    event_limit: int | None = None
    frame_limit: int | None = None
    anchors: dict[str, tuple[BlockKey, int]] = {}
    events = []
    end: tuple[int, int, int, int, int, int] | None = None
    started = False

    for line in lines:
        if match := ABORT.search(line):
            raise TraceFormatError(f"post-frame capture aborted: {match.group(1)}")
        if match := START.search(line):
            if started:
                raise TraceFormatError("multiple post-frame start markers")
            started = True
            event_limit, frame_limit = map(int, match.groups())
            continue
        if match := ANCHOR.search(line):
            if not started or events:
                raise TraceFormatError("post-frame anchors must precede execution events")
            identity, cycle = _anchor(match)
            if identity.processor in anchors:
                raise TraceFormatError(f"duplicate {identity.processor} continuation anchor")
            anchors[identity.processor] = (identity, cycle)
            continue
        if match := EVENT.search(line):
            if not started or set(anchors) != {"scpu", "sa1"}:
                raise TraceFormatError("post-frame events require both continuation anchors")
            events.append(_parse_event(match))
            continue
        if match := END.search(line):
            if end is not None:
                raise TraceFormatError("multiple post-frame end markers")
            end = tuple(map(int, match.groups()))

    if not started or event_limit is None or frame_limit is None:
        raise TraceFormatError("missing post-frame start marker")
    if not 1 <= frame_limit <= 10_000:
        raise TraceFormatError("frame limit is outside the sanitizer safety range")
    if set(anchors) != {"scpu", "sa1"}:
        raise TraceFormatError("post-frame capture requires S-CPU and SA-1 anchors")
    if end is None:
        raise TraceFormatError("missing visible-frame completion marker")
    count, frames_elapsed, width, height, pixel_count, nonblack = end
    if count != len(events):
        raise TraceFormatError("visible-frame marker count does not match execution events")
    if not 1 <= frames_elapsed <= frame_limit:
        raise TraceFormatError("visible frame lies outside the declared frame window")
    if width < 1 or height < 1 or pixel_count != width * height:
        raise TraceFormatError("visible-frame dimensions and pixel count disagree")
    if not 1 <= nonblack <= pixel_count:
        raise TraceFormatError("visible-frame marker does not prove a nonblack frame")
    for event in events:
        anchor_cycle = anchors[event.block.processor][1]
        if event.cycle < anchor_cycle:
            raise TraceFormatError(
                f"{event.block.processor} event precedes its continuation anchor"
            )

    continuation_from = [
        {**anchors[name][0].to_dict(), "cycle": anchors[name][1]}
        for name in ("scpu", "sa1")
    ]
    return summarize_events(
        events, event_limit, "complete",
        capture_extra={
            "route_segment": {
                "contract": CONTRACT,
                "kind": "continuation",
                "start_event": "first_end_frame",
                "stop_event": "first_visible_frame",
                "frames_elapsed": frames_elapsed,
                "continuation_from": continuation_from,
                "visible_frame": {
                    "width": width,
                    "height": height,
                    "pixel_count": pixel_count,
                    "nonblack_pixels": nonblack,
                },
            }
        },
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="private raw MesenCE log")
    parser.add_argument("output", type=Path, help="ROM-free continuation coverage JSON")
    args = parser.parse_args(argv)
    try:
        result = import_post_frame_lines(
            args.input.read_text(encoding="utf-8", errors="replace").splitlines()
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, TraceFormatError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
