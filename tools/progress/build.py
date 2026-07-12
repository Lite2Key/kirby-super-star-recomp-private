#!/usr/bin/env python3
"""Build the local progress dashboard from a ROM-free evidence manifest."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
from typing import Any

STATUSES = {"not_started", "in_progress", "blocked", "passed", "failed"}
DENOMINATORS = {"fixed", "evolving"}


class ManifestError(ValueError):
    """The progress manifest violates a dashboard invariant."""


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read {path}: {exc}") from exc
    validate_manifest(data)
    return data


def _unique(items: list[dict[str, Any]], collection: str) -> None:
    ids = [item.get("id") for item in items]
    if any(not isinstance(item_id, str) or not item_id for item_id in ids):
        raise ManifestError(f"{collection} contains an empty or non-string id")
    duplicates = sorted({item_id for item_id in ids if ids.count(item_id) > 1})
    if duplicates:
        raise ManifestError(f"duplicate {collection} ids: {', '.join(duplicates)}")


def validate_manifest(data: dict[str, Any]) -> None:
    required = {"schema_version", "snapshot", "milestones", "components", "scenarios", "blockers", "next_proof", "trends"}
    if not isinstance(data, dict):
        raise ManifestError("manifest root must be an object")
    missing = sorted(required - data.keys())
    if missing:
        raise ManifestError(f"missing root fields: {', '.join(missing)}")
    if data["schema_version"] != 1:
        raise ManifestError("unsupported schema_version; expected 1")

    for name in ("milestones", "components", "scenarios", "blockers", "trends"):
        if not isinstance(data[name], list):
            raise ManifestError(f"{name} must be an array")
    for name in ("milestones", "components", "scenarios"):
        _unique(data[name], name)

    for collection in ("milestones", "components", "scenarios"):
        for item in data[collection]:
            status = item.get("status")
            if status not in STATUSES:
                raise ManifestError(f"{collection}/{item.get('id', '?')} has invalid status {status!r}")
            if status == "passed" and not item.get("evidence"):
                raise ManifestError(f"{collection}/{item.get('id')} cannot pass without evidence")

    for component in data["components"]:
        metrics = component.get("metrics")
        if not isinstance(metrics, list):
            raise ManifestError(f"components/{component['id']} metrics must be an array")
        for metric in metrics:
            value, total = metric.get("value"), metric.get("total")
            if not isinstance(value, int) or not isinstance(total, int) or value < 0 or total < 0:
                raise ManifestError(f"components/{component['id']} metric counts must be non-negative integers")
            if value > total:
                raise ManifestError(f"components/{component['id']} metric value exceeds total")
            if metric.get("denominator") not in DENOMINATORS:
                raise ManifestError(f"components/{component['id']} metric denominator must be fixed or evolving")

    snapshot = data["snapshot"]
    for field in ("generated_at", "commit", "rom_id", "toolchain_manifest_hash"):
        if not isinstance(snapshot.get(field), str) or not snapshot[field]:
            raise ManifestError(f"snapshot.{field} must be a non-empty string")
    if not isinstance(snapshot.get("dirty"), bool):
        raise ManifestError("snapshot.dirty must be a boolean")
    evidence = snapshot.get("evidence")
    if not isinstance(evidence, dict) or set(evidence) != {"tests", "builds"}:
        raise ManifestError("snapshot.evidence requires tests and builds arrays")
    for kind in ("tests", "builds"):
        if not isinstance(evidence[kind], list):
            raise ManifestError(f"snapshot.evidence.{kind} must be an array")
        _unique(evidence[kind], f"snapshot evidence {kind}")
    next_proof = data["next_proof"]
    if not isinstance(next_proof, dict) or not next_proof.get("title") or not next_proof.get("acceptance"):
        raise ManifestError("next_proof requires title and non-empty acceptance criteria")


def render_dashboard(data: dict[str, Any], template_path: Path) -> str:
    template = template_path.read_text(encoding="utf-8")
    marker = "__PROGRESS_DATA__"
    if template.count(marker) != 1:
        raise ManifestError(f"dashboard template must contain exactly one {marker} marker")
    embedded = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    return template.replace(marker, embedded)


def render_markdown(data: dict[str, Any]) -> str:
    snap = data["snapshot"]
    lines = [
        "# Recompilation progress",
        "",
        "> This is a ROM-free evidence snapshot. Counts marked `evolving` are discovered inventories, not estimates of total project completion.",
        "",
        f"Snapshot: `{snap['generated_at']}` | commit `{snap['commit']}` | tree `{'dirty' if snap['dirty'] else 'clean'}`",
        "",
        "## Milestone map",
        "",
        "| Gate | Status | Evidence requirement |",
        "|---|---|---|",
    ]
    for milestone in data["milestones"]:
        lines.append(f"| {milestone['id']} - {milestone['name']} | `{milestone['status']}` | {milestone['gate']} |")
    lines += ["", "## Component counters", "", "| Component | Counter | Progress | Denominator |", "|---|---|---:|---|"]
    for component in data["components"]:
        if not component["metrics"]:
            lines.append(f"| {component['name']} | - | - | - |")
        for metric in component["metrics"]:
            total = metric["total"]
            progress = f"{metric['value']} / {total}" if total else "0 / discovered 0"
            lines.append(f"| {component['name']} | {metric['label']} | {progress} | `{metric['denominator']}` |")
    lines += ["", "## Snapshot evidence", ""]
    for kind in ("tests", "builds"):
        lines.append(f"**{kind.title()}**")
        evidence = snap["evidence"][kind]
        lines.extend(f"- `{item['id']}` [{item['label']}]({item['path']})" for item in evidence)
        if not evidence:
            lines.append("- No evidence recorded.")
        lines.append("")
    lines += ["## Next executable proof", "", f"**{data['next_proof']['title']}** - owner: `{data['next_proof']['owner']}`", ""]
    lines.extend(f"- [ ] {criterion}" for criterion in data["next_proof"]["acceptance"])
    lines += ["", "## Active blockers", ""]
    if data["blockers"]:
        lines.extend(f"- **{item['severity']}** `{item['id']}`: {item['summary']} (owner: {item['owner']})" for item in data["blockers"])
    else:
        lines.append("No recorded blockers in this snapshot.")
    lines += ["", "Regenerate the interactive dashboard with:", "", "```powershell", "python tools/progress/build.py --evidence progress/progress.json --out progress/site --markdown PROGRESS.md", "```", ""]
    return "\n".join(lines)


def build(
    evidence: Path,
    out_dir: Path,
    template: Path,
    markdown: Path | None = None,
    *,
    check: bool = False,
) -> None:
    data = load_manifest(evidence)
    outputs = {
        out_dir / "index.html": render_dashboard(data, template),
        out_dir / "progress.json": json.dumps(data, indent=2, ensure_ascii=False) + "\n",
    }
    if markdown is not None:
        outputs[markdown] = render_markdown(data)
    if check:
        stale = [
            path
            for path, expected in outputs.items()
            if not path.exists() or path.read_text(encoding="utf-8") != expected
        ]
        if stale:
            raise ManifestError(
                "generated progress outputs are stale: "
                + ", ".join(str(path) for path in stale)
            )
        return
    for path, rendered in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(rendered, encoding="utf-8")


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, default=root / "progress" / "progress.json")
    parser.add_argument("--out", type=Path, default=root / "progress" / "site")
    parser.add_argument("--template", type=Path, default=root / "progress" / "template.html")
    parser.add_argument("--markdown", type=Path, help="also regenerate the ROM-free Markdown snapshot")
    parser.add_argument("--check", action="store_true", help="fail if generated outputs are stale")
    args = parser.parse_args()
    try:
        build(args.evidence, args.out, args.template, args.markdown, check=args.check)
    except (ManifestError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
