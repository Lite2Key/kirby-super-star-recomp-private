"""Launch MesenCE with a deduplicated, private environment on Windows."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesen", type=Path, required=True)
    parser.add_argument("--lua", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--raw-log", type=Path, required=True)
    parser.add_argument("--home", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    for path in (args.mesen, args.lua, args.rom):
        if not path.is_file():
            parser.error(f"missing input file: {path}")
    if not 1 <= args.timeout <= 300:
        parser.error("timeout must be between 1 and 300 seconds")

    args.raw_log.parent.mkdir(parents=True, exist_ok=True)
    args.home.mkdir(parents=True, exist_ok=True)
    clean_env: dict[str, str] = {}
    for name in ("SYSTEMROOT", "WINDIR", "TEMP", "TMP", "COMSPEC"):
        value = os.environ.get(name)
        if value:
            clean_env[name] = value
    clean_env.update({
        "PATH": os.pathsep.join((str(Path(os.environ.get("SYSTEMROOT", r"C:\Windows")) / "System32"), os.environ.get("SYSTEMROOT", r"C:\Windows"))),
        "HOME": str(args.home),
        "USERPROFILE": str(args.home),
        "APPDATA": str(args.home),
        "LOCALAPPDATA": str(args.home),
    })

    command = [
        str(args.mesen),
        "--testRunner",
        "--enableStdout",
        f"--timeout={args.timeout}",
        str(args.lua),
        str(args.rom),
    ]
    with args.raw_log.open("w", encoding="utf-8", errors="replace") as output:
        result = subprocess.run(
            command,
            env=clean_env,
            stdout=output,
            stderr=subprocess.STDOUT,
            timeout=args.timeout + 15,
            check=False,
        )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
