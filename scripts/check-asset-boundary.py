"""Fail CI if tracked files look like protected reference artifacts."""

from __future__ import annotations

import pathlib
import subprocess
import sys


DENIED_SUFFIXES = {
    ".sfc", ".smc", ".srm", ".sav", ".state", ".mss", ".spc", ".bps", ".ips"
}
DENIED_PARTS = {".private", "generated", "ghidra-projects", "traces", "captures", "extracted"}


def tracked_files() -> list[pathlib.Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"], check=True, stdout=subprocess.PIPE
    )
    return [pathlib.Path(raw.decode()) for raw in result.stdout.split(b"\0") if raw]


def main() -> int:
    violations = [
        path
        for path in tracked_files()
        if path.suffix.lower() in DENIED_SUFFIXES or DENIED_PARTS.intersection(path.parts)
    ]
    if violations:
        print("Protected/reference artifacts are tracked:", file=sys.stderr)
        for path in violations:
            print(f"  {path}", file=sys.stderr)
        return 1
    print("Asset boundary check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
