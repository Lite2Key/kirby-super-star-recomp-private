"""Build private static-recompiler outputs from an authorized external ROM.

Every ROM-derived output is required to live below a directory named
``.private``.  The committed inputs remain value-free coverage, vector, and
identity metadata; instruction bytes exist only in the ignored private tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .generated_blocks import render as render_generated_blocks
from .coverage_corpus import merge_coverages
from .reset_lift import lift_reset_paths
from .rom import identify
from .trace_cfg import build_trace_seeded_cfg


EXPECTED_KSS_SHA256 = "4E095FBBDEC4A16B075D7140385FF68B259870CA9E3357F076DFFF7F3D1C4A62"


class PrivateBuildError(ValueError):
    """The requested private generation is unsafe or inconsistent."""


def _json(value: Mapping[str, Any]) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def _is_private_path(path: Path) -> bool:
    return ".private" in path.resolve().parts


def build_outputs(
    rom: bytes,
    vectors: Mapping[str, Any],
    coverage: Mapping[str, Any],
    *,
    expected_sha256: str = EXPECTED_KSS_SHA256,
    max_blocks_per_processor: int = 4096,
) -> dict[str, str]:
    identity = identify(rom)
    if identity.sha256.upper() != expected_sha256.upper():
        raise PrivateBuildError("ROM SHA-256 is not the configured supported revision")

    trace_cfg = build_trace_seeded_cfg(coverage)
    lifted = lift_reset_paths(
        vectors, trace_cfg, rom,
        max_blocks_per_processor=max_blocks_per_processor,
    )
    header, source, generated_manifest = render_generated_blocks(
        lifted,
        max_blocks_per_processor=max_blocks_per_processor,
        registration_name="private_first_frame",
        header_include="kss/generated_private_first_frame_blocks.hpp",
    )
    decoded = sum(item.get("status") == "decoded" for item in lifted["blocks"])
    unresolved = len(lifted["blocks"]) - decoded
    summary = {
        "schema_version": 1,
        "rom_id": f"kss-usa-rev0-sha256:{identity.sha256[:8].lower()}...{identity.sha256[-5:].lower()}",
        "coverage_sha256": hashlib.sha256(
            json.dumps(coverage, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        "observed_blocks": len(coverage.get("blocks", [])),
        "decoded_blocks": decoded,
        "unresolved_blocks": unresolved,
        "registered_blocks": generated_manifest["registered_blocks"],
        "identity_policy": generated_manifest["identity_policy"],
        "output_policy": "rom-derived-files-under-.private-only",
    }
    return {
        "trace-cfg.json": _json(trace_cfg),
        "lifted.json": _json(lifted),
        "include/kss/generated_private_first_frame_blocks.hpp": header,
        "src/private_first_frame_blocks.cpp": source,
        "manifest.json": _json(generated_manifest),
        "summary.json": _json(summary),
    }


def write_outputs(root: Path, outputs: Mapping[str, str], *, check: bool = False) -> None:
    if not _is_private_path(root):
        raise PrivateBuildError("ROM-derived output root must be below a .private directory")
    stale = []
    for relative, content in sorted(outputs.items()):
        path = root / relative
        if check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                stale.append(relative)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    if stale:
        raise PrivateBuildError("stale or missing private outputs: " + ", ".join(stale))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--vectors", type=Path, default=Path("analysis/vectors/reset-vectors.json"))
    parser.add_argument("--coverage", type=Path, action="append")
    parser.add_argument("--out", type=Path, default=Path(".private/generated/first-frame"))
    parser.add_argument("--max-blocks", type=int, default=4096)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        coverage_paths = args.coverage or [Path("analysis/coverage/first-frame-dual.json")]
        route_documents = [
            (path.stem, json.loads(path.read_text(encoding="utf-8")))
            for path in coverage_paths
        ]
        coverage = (route_documents[0][1] if len(route_documents) == 1
                    else merge_coverages(route_documents))
        outputs = build_outputs(
            args.rom.read_bytes(),
            json.loads(args.vectors.read_text(encoding="utf-8")),
            coverage,
            max_blocks_per_processor=args.max_blocks,
        )
        write_outputs(args.out, outputs, check=args.check)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
