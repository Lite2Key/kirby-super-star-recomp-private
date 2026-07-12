#!/usr/bin/env python3
"""Refresh deterministic, ROM-free repository evidence in progress.json."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

if __package__:
    from .build import ManifestError, validate_manifest
else:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from build import ManifestError, validate_manifest

EVIDENCE_ID = re.compile(r"^[a-z0-9][a-z0-9._-]*$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def _git(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args], cwd=repo, text=True, encoding="utf-8",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )


def git_commit(repo: Path) -> str:
    result = _git(repo, "rev-parse", "--verify", "HEAD")
    if result.returncode == 0:
        return result.stdout.strip()
    probe = _git(repo, "rev-parse", "--is-inside-work-tree")
    if probe.returncode == 0 and probe.stdout.strip() == "true":
        return "unborn"
    raise ManifestError(f"not a git worktree: {repo}")


def git_dirty(repo: Path, ignored: Iterable[Path]) -> bool:
    result = _git(repo, "status", "--porcelain=v1", "--untracked-files=all")
    if result.returncode:
        raise ManifestError(f"cannot read git status: {result.stderr.strip()}")
    ignored_names: set[str] = set()
    ignored_prefixes: set[str] = set()
    for path in ignored:
        try:
            name = path.resolve().relative_to(repo.resolve()).as_posix()
        except ValueError:
            continue
        if path.suffix:
            ignored_names.add(name)
        else:
            ignored_prefixes.add(name.rstrip("/") + "/")
    for line in result.stdout.splitlines():
        if len(line) < 4:
            continue
        candidate = line[3:].split(" -> ")[-1].replace("\\", "/").strip('"')
        if candidate in ignored_names or any(candidate.startswith(prefix) for prefix in ignored_prefixes):
            continue
        return True
    return False


def _evidence(items: list[list[str]]) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for evidence_id, label, path in items:
        if not EVIDENCE_ID.fullmatch(evidence_id):
            raise ManifestError(f"invalid evidence id {evidence_id!r}")
        if not label.strip() or not path.strip():
            raise ManifestError("evidence label and path must be non-empty")
        result.append({"id": evidence_id, "label": label, "path": path.replace("\\", "/")})
    return result


def _merge_evidence(existing: list[dict[str, str]], additions: list[dict[str, str]]) -> list[dict[str, str]]:
    merged = {item["id"]: item for item in existing}
    merged.update({item["id"]: item for item in additions})
    return [merged[key] for key in sorted(merged)]


def derive_snapshot(
    data: dict[str, Any], *, repo: Path, manifest_path: Path,
    toolchain_path: Path, test_evidence: list[list[str]],
    build_evidence: list[list[str]], clear_evidence: bool = False,
) -> dict[str, Any]:
    updated = copy.deepcopy(data)
    snapshot = updated["snapshot"]
    current_evidence = snapshot.get("evidence", {"tests": [], "builds": []})
    if clear_evidence:
        current_evidence = {"tests": [], "builds": []}
    snapshot["commit"] = git_commit(repo)
    snapshot["toolchain_manifest_hash"] = sha256_file(toolchain_path)
    snapshot["evidence"] = {
        "tests": _merge_evidence(current_evidence.get("tests", []), _evidence(test_evidence)),
        "builds": _merge_evidence(current_evidence.get("builds", []), _evidence(build_evidence)),
    }
    snapshot["dirty"] = git_dirty(
        repo,
        ignored=(manifest_path, repo / "progress" / "site", repo / "PROGRESS.md"),
    )
    validate_manifest(updated)
    return updated


def update(
    manifest_path: Path, toolchain_path: Path, repo: Path, *,
    test_evidence: list[list[str]] | None = None,
    build_evidence: list[list[str]] | None = None,
    clear_evidence: bool = False, check: bool = False,
    generated_at: str | None = None,
) -> bool:
    try:
        current = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read {manifest_path}: {exc}") from exc
    if not isinstance(current, dict) or not isinstance(current.get("snapshot"), dict):
        raise ManifestError("manifest requires a snapshot object")
    expected = derive_snapshot(
        current, repo=repo, manifest_path=manifest_path,
        toolchain_path=toolchain_path,
        test_evidence=test_evidence or [], build_evidence=build_evidence or [],
        clear_evidence=clear_evidence,
    )
    comparable_current = copy.deepcopy(current)
    comparable_expected = copy.deepcopy(expected)
    comparable_expected["snapshot"]["generated_at"] = comparable_current["snapshot"]["generated_at"]
    changed = comparable_current != comparable_expected
    if check:
        if changed:
            fields = [
                field for field in ("commit", "dirty", "toolchain_manifest_hash", "evidence")
                if current["snapshot"].get(field) != expected["snapshot"].get(field)
            ]
            raise ManifestError(f"progress snapshot is stale: {', '.join(fields)}")
        return False
    if not changed:
        return False
    timestamp = generated_at or datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    expected["snapshot"]["generated_at"] = timestamp
    manifest_path.write_text(json.dumps(expected, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return True


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=root / "progress" / "progress.json")
    parser.add_argument("--toolchain", type=Path, default=root / "config" / "toolchain.lock.json")
    parser.add_argument("--repo", type=Path, default=root)
    parser.add_argument("--test-evidence", nargs=3, action="append", default=[], metavar=("ID", "LABEL", "PATH"))
    parser.add_argument("--build-evidence", nargs=3, action="append", default=[], metavar=("ID", "LABEL", "PATH"))
    parser.add_argument("--clear-evidence", action="store_true")
    parser.add_argument("--generated-at", help="explicit ISO-8601 timestamp for reproducible fixtures")
    parser.add_argument("--check", action="store_true", help="fail without writing when snapshot fields are stale")
    args = parser.parse_args()
    try:
        changed = update(
            args.manifest, args.toolchain, args.repo,
            test_evidence=args.test_evidence, build_evidence=args.build_evidence,
            clear_evidence=args.clear_evidence, check=args.check,
            generated_at=args.generated_at,
        )
    except (ManifestError, OSError) as exc:
        parser.error(str(exc))
    if not args.check:
        print("progress snapshot updated" if changed else "progress snapshot already current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
