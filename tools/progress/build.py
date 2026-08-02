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
BLOCK_STAGES = (
    "observed", "lifted", "generated", "semantics_supported",
    "executed", "reference_verified",
)


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
    required = {"schema_version", "snapshot", "milestones", "workstreams", "components", "scenarios", "blockers", "next_proof", "trends"}
    if not isinstance(data, dict):
        raise ManifestError("manifest root must be an object")
    missing = sorted(required - data.keys())
    if missing:
        raise ManifestError(f"missing root fields: {', '.join(missing)}")
    if data["schema_version"] != 1:
        raise ManifestError("unsupported schema_version; expected 1")

    for name in ("milestones", "workstreams", "components", "scenarios", "blockers", "trends"):
        if not isinstance(data[name], list):
            raise ManifestError(f"{name} must be an array")
    for name in ("milestones", "workstreams", "components", "scenarios"):
        _unique(data[name], name)

    for workstream in data["workstreams"]:
        if not isinstance(workstream.get("name"), str) or not workstream["name"]:
            raise ManifestError("workstream requires a name")
        checkpoints = workstream.get("checkpoints")
        if not isinstance(checkpoints, list) or not checkpoints:
            raise ManifestError(f"workstreams/{workstream['id']} requires checkpoints")
        names = [checkpoint.get("name") for checkpoint in checkpoints]
        if any(not isinstance(name, str) or not name for name in names):
            raise ManifestError(f"workstreams/{workstream['id']} has an invalid checkpoint name")
        if len(names) != len(set(names)):
            raise ManifestError(f"workstreams/{workstream['id']} has duplicate checkpoints")
        for checkpoint in checkpoints:
            if checkpoint.get("status") not in STATUSES:
                raise ManifestError(
                    f"workstreams/{workstream['id']} has an invalid checkpoint status"
                )

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


def _read_artifact(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read block-map artifact {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ManifestError(f"block-map artifact root must be an object: {path}")
    return value


def _identity_key(identity: dict[str, Any]) -> tuple[str, int, bool, bool, bool]:
    try:
        mode = identity["mode"]
        result = (
            identity["processor"], identity["pc"], mode["emulation"], mode["m8"], mode["x8"]
        )
    except (KeyError, TypeError) as exc:
        raise ManifestError("block-map artifact contains a malformed identity") from exc
    if result[0] not in {"scpu", "sa1"} or not isinstance(result[1], int):
        raise ManifestError("block-map identity has an invalid processor or PC")
    if not all(isinstance(flag, bool) for flag in result[2:]):
        raise ManifestError("block-map identity mode flags must be booleans")
    return result


def load_block_map(root: Path) -> dict[str, Any]:
    """Derive a ROM-byte-free tile map from committed sanitized artifacts."""
    cfg_root = root / "analysis" / "cfg"
    trace = _read_artifact(cfg_root / "first-frame-dual.trace-cfg.json")
    lifted = _read_artifact(cfg_root / "first-frame-dual.lifted.json")
    generated = _read_artifact(cfg_root / "first-frame-dual.generated-blocks.json")
    frontier = _read_artifact(cfg_root / "first-frame-dual.frontier.json")
    bootstrap = _read_artifact(cfg_root / "bootstrap-dual.trace-cfg.json")
    reference = _read_artifact(root / "analysis" / "differential" / "reset-block-reference.json")
    scpu_reference = _read_artifact(
        root / "analysis" / "differential" / "scpu-reference-identity-coverage.json"
    )
    sa1_reference = _read_artifact(
        root / "analysis" / "differential" / "sa1-post-reset-reference.json"
    )
    execution = _read_artifact(
        root / "analysis" / "coverage" / "boot-probe-identity-coverage.json"
    )

    try:
        observations = trace["observations"]["blocks"]
        lifted_blocks = lifted["blocks"]
        bootstrap_blocks = bootstrap["cfg"]["blocks"]
        generated_counts = generated["processors"]
        frontier_processors = frontier["processors"]
        references = reference["blocks"]
        scpu_reference_items = scpu_reference["newly_reference_verified_identities"]
        sa1_reference_items = sa1_reference["architectural_slice"]["identities"]
        execution_missing_items = execution["missing_identities"]
    except (KeyError, TypeError) as exc:
        raise ManifestError("block-map artifact is missing a required collection") from exc
    if not all(isinstance(items, list) for items in (observations, lifted_blocks, bootstrap_blocks, references)):
        raise ManifestError("block-map artifact collections must be arrays")

    observed = {_identity_key(item["identity"]): item for item in observations}
    lifted_by_key = {_identity_key(item["identity"]): item for item in lifted_blocks}
    if len(observed) != len(observations) or len(lifted_by_key) != len(lifted_blocks):
        raise ManifestError("block-map artifacts contain duplicate identities")
    if set(observed) != set(lifted_by_key):
        raise ManifestError("first-frame trace and lift inventories disagree")
    bootstrap_keys = {_identity_key(item["identity"]) for item in bootstrap_blocks}
    reference_windows = {
        item["processor"]: (item["cycle_start"], item["cycle_end"])
        for item in references
    }
    if set(reference_windows) != {"scpu", "sa1"}:
        raise ManifestError("reset reference must cover both processors")

    expanded_reference = {
        _identity_key(item) for item in scpu_reference_items + sa1_reference_items
    }
    if len(expanded_reference) != len(scpu_reference_items) + len(sa1_reference_items):
        raise ManifestError("expanded reference artifacts contain duplicate identities")
    if not expanded_reference <= set(observed):
        raise ManifestError("expanded reference artifacts contain identities outside inventory")
    if scpu_reference.get("newly_reference_verified_count") != len(scpu_reference_items) \
            or scpu_reference.get("reference_verified_count") != 214:
        raise ManifestError("S-CPU reference coverage counts disagree")
    if sa1_reference.get("architectural_slice", {}).get("state_verified_count") \
            != len(sa1_reference_items):
        raise ManifestError("SA-1 architectural reference counts disagree")

    execution_missing = {_identity_key(item) for item in execution_missing_items}
    if len(execution_missing) != len(execution_missing_items):
        raise ManifestError("execution coverage contains duplicate missing identities")
    if not execution_missing <= set(observed):
        raise ManifestError("execution coverage contains identities outside the inventory")
    execution_count = execution.get("executed_count")
    missing_count = execution.get("missing_count")
    if execution.get("inventory_count") != len(observed) \
            or missing_count != len(execution_missing) \
            or execution_count != len(observed) - len(execution_missing):
        raise ManifestError("execution coverage counts disagree with the first-frame inventory")
    execution_covered = set(observed) - execution_missing

    unsupported: dict[str, set[int]] = {}
    for processor in ("scpu", "sa1"):
        try:
            unsupported[processor] = {
                item["opcode"] for item in frontier_processors[processor]["unsupported_opcodes"]
            }
        except (KeyError, TypeError) as exc:
            raise ManifestError("frontier artifact lacks unsupported-opcode inventory") from exc

    processors: dict[str, Any] = {}
    for processor in ("scpu", "sa1"):
        blocks = []
        for key, observation in observed.items():
            if key[0] != processor:
                continue
            lifted_item = lifted_by_key[key]
            first_cycle = observation.get("first_cycle")
            hits = observation.get("hits")
            if not isinstance(first_cycle, int) or not isinstance(hits, int) or hits < 1:
                raise ManifestError("trace observation has invalid cycle or hit counts")
            stage = "generated" if lifted_item["instruction"]["opcode"] in unsupported[processor] \
                else "semantics_supported"
            if key in execution_covered and stage == "semantics_supported":
                stage = "executed"
            start_cycle, end_cycle = reference_windows[processor]
            if start_cycle <= first_cycle <= end_cycle or key in expanded_reference:
                stage = "reference_verified"
            mode = {"emulation": key[2], "m8": key[3], "x8": key[4]}
            blocks.append({
                "pc": key[1],
                "mode": mode,
                "stage": stage,
                "hits": hits,
                "first_cycle": first_cycle,
                "evolving": key not in bootstrap_keys,
                "frontier": lifted_item["instruction"]["opcode"] in unsupported[processor],
            })
        blocks.sort(key=lambda item: (item["first_cycle"], item["pc"], tuple(item["mode"].values())))
        if generated_counts.get(processor) != len(blocks):
            raise ManifestError(f"generated {processor} count disagrees with trace inventory")
        cumulative = {
            "observed": len(blocks),
            "lifted": len(blocks),
            "generated": len(blocks),
            "semantics_supported": sum(
                item["stage"] in {"semantics_supported", "executed", "reference_verified"}
                for item in blocks
            ),
            "executed": sum(
                item["stage"] in {"executed", "reference_verified"} for item in blocks
            ),
            "reference_verified": sum(item["stage"] == "reference_verified" for item in blocks),
        }
        processors[processor] = {
            "blocks": blocks,
            "counts": cumulative,
            "evolving": sum(item["evolving"] for item in blocks),
            "frontier": sum(item["frontier"] for item in blocks),
        }

    return {
        "boundary": frontier.get("boundary"),
        "stages": list(BLOCK_STAGES),
        "processors": processors,
        "execution_count": execution_count,
        "observed_count": len(observed),
        "sources": [
            "analysis/cfg/first-frame-dual.trace-cfg.json",
            "analysis/cfg/first-frame-dual.lifted.json",
            "analysis/cfg/first-frame-dual.generated-blocks.json",
            "analysis/cfg/first-frame-dual.frontier.json",
            "analysis/differential/reset-block-reference.json",
            "analysis/differential/scpu-reference-identity-coverage.json",
            "analysis/differential/sa1-post-reset-reference.json",
            "analysis/coverage/boot-probe-identity-coverage.json",
        ],
    }


def load_boundary_sync(root: Path) -> dict[str, Any]:
    """Derive the shared first-endFrame gap from the sanitized parity audit."""
    path = root / "analysis" / "differential" / "first-frame-parity-audit.json"
    audit = _read_artifact(path)
    sa1_domain = _read_artifact(
        root / "analysis" / "differential" / "sa1-first-endframe-domain.json"
    )
    chain_expectation = _read_artifact(
        root / "analysis" / "differential"
        / "first-endframe-event-chain-expectation.json"
    )
    chain_observation = _read_artifact(
        root / "analysis" / "differential"
        / "runtime-first-endframe-event-chain-observation.json"
    )
    live_scheduler = _read_artifact(
        root / "analysis" / "differential" / "live-first-frame-scheduler.json"
    )
    scpu_boundary = _read_artifact(
        root / "analysis" / "differential" / "scpu-first-endframe-boundary.json"
    )
    scpu_origin = _read_artifact(
        root / "analysis" / "differential" / "scpu-phase-divergence-origin.json"
    )
    spc_boundary = _read_artifact(
        root / "analysis" / "differential" / "spc-first-endframe-suspension.json"
    )
    try:
        reference = audit["hardware_reference"]
        target_master = reference["master_clock"]
        if any(candidate != target_master for candidate in (
            live_scheduler["reference"]["target_master_clock"],
            sa1_domain["reference"]["target_master_clock"],
            scpu_boundary["target_master_clock"],
            spc_boundary["clock_contract"]["target_master_clock"],
        )):
            raise ManifestError("first-frame boundary artifacts disagree on target master")
        if scpu_origin["accumulated_checkpoint_divergence"]["master_clock_shortfall"] \
                != 73312:
            raise ManifestError("S-CPU phase-origin evidence has an unexpected SA-1 shortfall")
        domains = [
            {
                "id": "scpu",
                "label": "S-CPU exact observation",
                "value": scpu_boundary["target_master_clock"],
                "target": target_master,
                "unit": "master clocks",
                "note": "exact access boundary represented; sequencer is five bytes ahead of reference",
            },
            {
                "id": "sa1",
                "label": "SA-1 post-reset poll frontier",
                "value": live_scheduler["runtime"]["sa1"]["frame_observation"]["ready_master_clock"],
                "target": target_master,
                "unit": "master clocks",
                "note": "reset, both MVNs, and the post-reset poll are cooperative through the frame; six master clocks remain inside the next whole instruction",
            },
            {
                "id": "spc",
                "label": "SPC exact observation",
                "value": spc_boundary["in_flight_observation"]["observed_master_clock"],
                "target": target_master,
                "unit": "master clocks",
                "note": "pending instruction represented without mutating the core",
            },
        ]
        expected_chains = chain_expectation["chains"]
        observed_chains = chain_observation["chains"]
        chain_rows = (
            ("cpu-writes", "CPU write chain", "cpu_writes"),
            ("spc-ports", "SPC port chain", "spc_ports"),
            ("ppu-events", "PPU event chain", "ppu_register_writes"),
            ("dma-events", "DMA event chain", "dma_register_writes"),
        )
        chains = []
        for row_id, label, key in chain_rows:
            observation = observed_chains[key]
            expected = expected_chains[key]["records"]
            if observation["reference_records"] != expected:
                raise ManifestError(f"event-chain reference count disagrees for {key}")
            chains.append({
                "id": row_id,
                "label": label,
                "value": observation["runtime_records"],
                "target": expected,
                "delta": observation["delta"],
                "matches": observation["matches"],
                "unit": "records",
                "note": "count and ordered digest must both match",
            })
    except (KeyError, TypeError) as exc:
        raise ManifestError("first-frame parity audit lacks boundary-sync counters") from exc
    for item in domains:
        if not isinstance(item["value"], int) or not isinstance(item["target"], int) \
                or item["value"] < 0 or item["target"] <= 0:
            raise ManifestError("first-frame parity audit has an invalid domain counter")
    for item in chains:
        if not isinstance(item["value"], int) or not isinstance(item["target"], int) \
                or item["value"] < 0 or item["target"] <= 0 \
                or item["delta"] != item["value"] - item["target"] \
                or not isinstance(item["matches"], bool):
            raise ManifestError("first-frame parity audit has an invalid event-chain counter")
    return {
        "boundary": reference["boundary"],
        "target_master": target_master,
        "full_parity_proven": audit["verdict"]["full_first_frame_parity_proven"],
        "domains": domains,
        "chains": chains,
        "sources": [
            "analysis/differential/first-frame-parity-audit.json",
            "analysis/differential/sa1-first-endframe-domain.json",
            "analysis/differential/first-endframe-event-chain-expectation.json",
            "analysis/differential/runtime-first-endframe-event-chain-observation.json",
            "analysis/differential/live-first-frame-scheduler.json",
            "analysis/differential/scpu-first-endframe-boundary.json",
            "analysis/differential/spc-first-endframe-suspension.json",
            "analysis/differential/scpu-phase-divergence-origin.json",
        ],
    }


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


def render_summary_svg(data: dict[str, Any]) -> str:
    """Render a script-free GitHub README summary from sanitized counters."""
    width, height = 1200, 720
    colors = {
        "passed": "#64d883", "in_progress": "#62d6b5",
        "blocked": "#ff6b82", "failed": "#ff6b82",
        "not_started": "#52617e", "observed": "#66738c",
        "lifted": "#3e91c9", "generated": "#8b67d5",
        "semantics_supported": "#4fb6a4", "executed": "#e99a45",
        "reference_verified": "#50c878",
    }
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#eef3ff}.muted{fill:#9cacca}.small{font-size:12px}.label{font-size:13px}.head{font-size:18px;font-weight:700}.title{font-size:27px;font-weight:800}.count{font-size:13px;font-weight:700}.panel{fill:#151c31;stroke:#2b395c;stroke-width:1}.track{fill:#0a0f1e}.identity,.checkpoint{stroke:#0b1020;stroke-width:1}</style>",
        f'<rect width="{width}" height="{height}" fill="#0b1020"/>',
        '<rect width="1200" height="150" fill="#101a31"/>',
        '<text x="42" y="42" class="title">Kirby Super Star static recompilation</text>',
        f'<text x="42" y="64" class="muted small">ROM-free evidence snapshot {html.escape(data["snapshot"]["generated_at"])} · M3 reset-to-first-frame</text>',
    ]
    for index, milestone in enumerate(data["milestones"]):
        x = 42 + index * 94
        color = colors[milestone["status"]]
        parts.extend((
            f'<rect x="{x}" y="82" width="89" height="43" rx="7" fill="#151c31" stroke="{color}"/>',
            f'<circle cx="{x + 12}" cy="95" r="5" fill="{color}"/>',
            f'<text x="{x + 22}" y="99" class="count">{html.escape(milestone["id"])}</text>',
            f'<text x="{x + 8}" y="116" class="small muted">{html.escape(milestone["status"].replace("_", " "))}</text>',
        ))

    block_map = data["block_map"]
    all_blocks = block_map["processors"]["scpu"]["blocks"] + block_map["processors"]["sa1"]["blocks"]
    components = {component["id"]: component for component in data["components"]}
    metrics = {
        component_id: {metric["label"]: metric for metric in component["metrics"]}
        for component_id, component in components.items()
    }
    route_total = metrics["route-corpus"]["union observed identities"]["value"]
    generated_total = metrics["decoder-lifter"]["private generated block functions"]["value"]
    # The first-frame identity artifact remains the exact event-chain map, but
    # the route wall should show the newest bounded private continuation when
    # one is recorded in the manifest. This keeps the visual route counter
    # honest without pretending that post-frame identities are first-frame
    # event evidence.
    latest_route = next(
        (trend for trend in reversed(data.get("trends", []))
         if isinstance(trend, dict) and "route_probe_executed" in trend),
        None,
    )
    runtime_total = (
        latest_route["route_probe_executed"]
        if latest_route else block_map["execution_count"]
    )
    semantic_total = (
        metrics["s-cpu"]["semantics-supported identities"]["value"]
        + metrics["sa-1"]["semantics-supported identities"]["value"]
    )
    reference_total = sum(
        processor["counts"]["reference_verified"]
        for processor in block_map["processors"].values()
    )
    parts.extend((
        '<rect x="28" y="145" width="1144" height="150" rx="12" class="panel"/>',
        f'<text x="48" y="171" class="head">{route_total:,}-identity first-visible route wall</text>',
        f'<text x="48" y="190" class="muted small">One tile per processor + PC + mode identity · {route_total - generated_total:,} newly discovered tiles remain</text>',
    ))
    for index in range(route_total):
        x = 48 + (index % 100) * 5
        y = 198 + (index // 100) * 4
        stage = (
            "reference_verified" if index < reference_total else
            "executed" if index < runtime_total else
            "semantics_supported" if index < semantic_total else "not_started"
        )
        parts.append(f'<rect class="identity" x="{x}" y="{y}" width="4" height="3" rx="0.5" fill="{colors[stage]}"/>')
    proof_counts = {
        "semantics_supported": semantic_total,
        "executed": runtime_total,
        "reference_verified": reference_total,
    }
    for index, (stage, label) in enumerate((
        ("semantics_supported", "Semantic execution"),
        ("executed", "Runtime execution"),
        ("reference_verified", "Reference oracle"),
    )):
        y = 201 + index * 29
        value = proof_counts[stage]
        parts.extend((
            f'<text x="505" y="{y + 10}" class="label">{label}</text>',
            f'<rect x="665" y="{y}" width="390" height="13" rx="6" class="track"/>',
            f'<rect x="665" y="{y}" width="{390 * value / route_total:.1f}" height="13" rx="6" fill="{colors[stage]}"/>',
            f'<text x="1070" y="{y + 11}" class="count">{value} / {route_total}</text>',
        ))

    sync = data["boundary_sync"]
    parts.extend((
        '<rect x="28" y="307" width="557" height="185" rx="12" class="panel"/>',
        '<rect x="597" y="307" width="575" height="185" rx="12" class="panel"/>',
        '<text x="48" y="336" class="head">Clock domains at master 306,900</text>',
        '<text x="617" y="336" class="head">Ordered event chains</text>',
    ))

    def add_bar(x: int, y: int, bar_width: int, item: dict[str, Any], mismatch: bool) -> None:
        ratio = min(1.0, item["value"] / item["target"])
        fill = "#ff6b82" if mismatch else "#62d6b5"
        parts.extend((
            f'<text x="{x}" y="{y}" class="label">{html.escape(item["label"])}</text>',
            f'<text x="{x + bar_width}" y="{y}" text-anchor="end" class="count">{item["value"]:,} / {item["target"]:,}</text>',
            f'<rect x="{x}" y="{y + 7}" width="{bar_width}" height="11" rx="5" class="track"/>',
            f'<rect x="{x}" y="{y + 7}" width="{bar_width * ratio:.1f}" height="11" rx="5" fill="{fill}"/>',
        ))

    for index, item in enumerate(sync["domains"]):
        add_bar(48, 365 + index * 39, 505, item, False)
    for index, item in enumerate(sync["chains"]):
        add_bar(617, 365 + index * 31, 523, item, not item["matches"])

    parts.extend((
        '<rect x="28" y="504" width="557" height="190" rx="12" class="panel"/>',
        '<rect x="597" y="504" width="575" height="190" rx="12" class="panel"/>',
        '<text x="48" y="533" class="head">42-checkpoint port atlas</text>',
        '<text x="617" y="533" class="head">Next executable proof</text>',
    ))
    for row, stream in enumerate(data["workstreams"]):
        y = 555 + row * 19
        parts.append(f'<text x="48" y="{y + 10}" class="small">{html.escape(stream["name"])}</text>')
        for column, checkpoint in enumerate(stream["checkpoints"]):
            x = 245 + column * 49
            parts.append(f'<rect class="checkpoint" x="{x}" y="{y}" width="45" height="13" rx="3" fill="{colors[checkpoint["status"]]}"/>')
    proof = data["next_proof"]
    parts.append(f'<text x="617" y="562" class="head" fill="#62d6b5">{html.escape(proof["title"])}</text>')
    for index, criterion in enumerate(proof["acceptance"][:4]):
        clipped = criterion if len(criterion) <= 76 else criterion[:73] + "..."
        parts.append(f'<text x="625" y="{591 + index * 24}" class="label">• {html.escape(clipped)}</text>')
    parts.extend((
        '<text x="617" y="681" class="small muted">Green = proven · teal = active · red = mismatch · gray = remaining</text>',
        '</svg>\n',
    ))
    return "".join(parts)


def build(
    evidence: Path,
    out_dir: Path,
    template: Path,
    markdown: Path | None = None,
    *,
    check: bool = False,
) -> None:
    data = load_manifest(evidence)
    root = evidence.resolve().parent.parent
    site_data = dict(data)
    site_data["block_map"] = load_block_map(root)
    site_data["boundary_sync"] = load_boundary_sync(root)
    outputs = {
        out_dir / "index.html": render_dashboard(site_data, template),
        out_dir / "progress.json": json.dumps(site_data, indent=2, ensure_ascii=False) + "\n",
        out_dir / "summary.svg": render_summary_svg(site_data),
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
