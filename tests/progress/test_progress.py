import copy
import importlib.util
import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("progress_build", ROOT / "tools" / "progress" / "build.py")
progress_build = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(progress_build)
SNAPSHOT_SPEC = importlib.util.spec_from_file_location("progress_snapshot", ROOT / "tools" / "progress" / "snapshot.py")
progress_snapshot = importlib.util.module_from_spec(SNAPSHOT_SPEC)
assert SNAPSHOT_SPEC.loader
SNAPSHOT_SPEC.loader.exec_module(progress_snapshot)


class ProgressTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / "progress" / "progress.json").read_text(encoding="utf-8"))

    def test_seed_manifest_is_valid(self):
        progress_build.validate_manifest(self.manifest)

    def test_build_embeds_dashboard_and_writes_machine_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "site"
            progress_build.build(ROOT / "progress" / "progress.json", out, ROOT / "progress" / "template.html")
            page = (out / "index.html").read_text(encoding="utf-8")
            self.assertNotIn("__PROGRESS_DATA__", page)
            self.assertIn("Kirby Super Star recompilation map", page)
            self.assertIn('\"denominator\":\"evolving\"', page)
            machine = json.loads((out / "progress.json").read_text(encoding="utf-8"))
            self.assertEqual(machine["schema_version"], 1)
            self.assertEqual(len(machine["block_map"]["processors"]["scpu"]["blocks"]), 214)
            self.assertEqual(len(machine["block_map"]["processors"]["sa1"]["blocks"]), 40)
            self.assertEqual(len(machine["workstreams"]), 7)
            self.assertEqual(machine["boundary_sync"]["target_master"], 306900)
            summary = (out / "summary.svg").read_text(encoding="utf-8")
            ET.fromstring(summary)
            self.assertEqual(summary.count('class="identity"'), 254)
            self.assertEqual(summary.count('class="checkpoint"'), 42)
            self.assertNotIn("<script", summary)
            self.assertNotIn("<image", summary)

    def test_block_map_is_derived_from_sanitized_first_frame_artifacts(self):
        block_map = progress_build.load_block_map(ROOT)
        self.assertEqual(block_map["boundary"], "first-snes-end-frame")
        scpu = block_map["processors"]["scpu"]
        sa1 = block_map["processors"]["sa1"]
        self.assertEqual(scpu["counts"], {
            "observed": 214, "lifted": 214, "generated": 214,
            "semantics_supported": 214,
            "executed": 214, "reference_verified": 214,
        })
        self.assertEqual(sa1["counts"], {
            "observed": 40, "lifted": 40, "generated": 40,
            "semantics_supported": 40,
            "executed": 40, "reference_verified": 34,
        })
        self.assertEqual((scpu["evolving"], sa1["evolving"]), (54, 17))
        self.assertEqual((scpu["frontier"], sa1["frontier"]), (0, 0))
        rendered = json.dumps(block_map)
        for forbidden in ("bytes_hex", "rom_offset", "opcode", "mnemonic", "operand"):
            self.assertNotIn(forbidden, rendered)

    def test_block_map_template_has_keyboard_detail_and_frontier_filters(self):
        template = (ROOT / "progress" / "template.html").read_text(encoding="utf-8")
        for required in (
            'id="block-map"', 'aria-label="Filter block tiles"',
            'role="listitem"', "focusin", 'aria-live="polite"',
            'data-map-filter="frontier"', 'data-map-filter="evolving"',
        ):
            self.assertIn(required, template)

    def test_boundary_sync_map_is_derived_from_sanitized_audit(self):
        sync = progress_build.load_boundary_sync(ROOT)
        self.assertFalse(sync["full_parity_proven"])
        self.assertEqual(
            [(item["id"], item["value"], item["target"]) for item in sync["domains"]],
            [("scpu", 306900, 306900), ("sa1", 225694, 306900),
             ("spc", 306900, 306900)],
        )
        self.assertEqual(
            [(item["id"], item["value"], item["target"]) for item in sync["chains"]],
            [("cpu-writes", 18679, 26906), ("spc-ports", 465, 462),
             ("ppu-events", 54, 54), ("dma-events", 23, 23)],
        )
        self.assertTrue(all(not item["matches"] for item in sync["chains"]))
        self.assertEqual(len(sync["sources"]), 8)
        self.assertIn("sa1-first-endframe-domain.json", sync["sources"][1])

    def test_boundary_sync_template_exposes_clock_and_event_gaps(self):
        template = (ROOT / "progress" / "template.html").read_text(encoding="utf-8")
        self.assertIn('id="boundary-sync"', template)
        self.assertIn("Hardware-boundary synchronization", template)
        self.assertIn("Event-chain proof", template)
        self.assertIn("ordered digest", template)

    def test_github_pages_workflow_publishes_only_the_rom_free_site(self):
        workflow = (ROOT / ".github" / "workflows" / "progress-pages.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("actions/upload-pages-artifact@v3", workflow)
        self.assertIn("actions/deploy-pages@v4", workflow)
        self.assertIn("path: progress/site", workflow)
        self.assertNotIn("path: .\n", workflow)

    def test_workstream_atlas_has_explicit_remaining_checkpoints(self):
        template = (ROOT / "progress" / "template.html").read_text(encoding="utf-8")
        self.assertIn('id="workstream-atlas"', template)
        self.assertIn('id="workstream-detail"', template)
        self.assertIn('class="workstream-step', template)
        remaining = [
            checkpoint
            for stream in self.manifest["workstreams"]
            for checkpoint in stream["checkpoints"]
            if checkpoint["status"] != "passed"
        ]
        self.assertGreater(len(remaining), 0)
        self.assertIn('id="auto-refresh"', template)
        self.assertIn("setTimeout(()=>location.reload(),30000)", template)

    def test_invalid_workstream_checkpoint_status_is_rejected(self):
        bad = copy.deepcopy(self.manifest)
        bad["workstreams"][0]["checkpoints"][0]["status"] = "almost"
        with self.assertRaisesRegex(progress_build.ManifestError, "checkpoint status"):
            progress_build.validate_manifest(bad)

    def test_markdown_calls_out_evolving_denominators(self):
        output = progress_build.render_markdown(self.manifest)
        self.assertIn("not estimates of total project completion", output)
        self.assertIn("`evolving`", output)
        self.assertIn("S-CPU control flow | observed mode-aware blocks | 214 / 214 | `evolving`", output)

    def test_duplicate_ids_are_rejected(self):
        bad = copy.deepcopy(self.manifest)
        bad["scenarios"].append(copy.deepcopy(bad["scenarios"][0]))
        with self.assertRaisesRegex(progress_build.ManifestError, "duplicate scenarios ids"):
            progress_build.validate_manifest(bad)

    def test_metric_cannot_overstate_its_total(self):
        bad = copy.deepcopy(self.manifest)
        bad["components"][0]["metrics"][0].update(value=65, total=64)
        with self.assertRaisesRegex(progress_build.ManifestError, "value exceeds total"):
            progress_build.validate_manifest(bad)

    def test_passed_gate_requires_evidence(self):
        bad = copy.deepcopy(self.manifest)
        bad["milestones"][0]["status"] = "passed"
        bad["milestones"][0].pop("evidence", None)
        with self.assertRaisesRegex(progress_build.ManifestError, "cannot pass without evidence"):
            progress_build.validate_manifest(bad)

    def test_json_embedding_neutralizes_script_end(self):
        malicious = copy.deepcopy(self.manifest)
        malicious["next_proof"]["title"] = "</script><script>alert(1)</script>"
        rendered = progress_build.render_dashboard(malicious, ROOT / "progress" / "template.html")
        self.assertNotIn("</script><script>alert(1)</script>", rendered)
        self.assertIn("<\\/script>", rendered)

    def test_snapshot_records_git_toolchain_and_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            manifest = temp / "progress.json"
            toolchain = temp / "toolchain.json"
            manifest.write_text(json.dumps(self.manifest), encoding="utf-8")
            toolchain.write_bytes(b'{"tool":"1"}\n')
            with mock.patch.object(progress_snapshot, "git_commit", return_value="a" * 40), mock.patch.object(progress_snapshot, "git_dirty", return_value=False):
                changed = progress_snapshot.update(
                    manifest, toolchain, temp,
                    test_evidence=[["tests.unit", "Unit tests", "evidence/tests.txt"]],
                    build_evidence=[["build.win", "Windows build", "evidence/build.txt"]],
                    generated_at="2026-07-12T20:00:00Z",
                )
            self.assertTrue(changed)
            result = json.loads(manifest.read_text(encoding="utf-8"))["snapshot"]
            self.assertEqual(result["commit"], "a" * 40)
            self.assertFalse(result["dirty"])
            self.assertRegex(result["toolchain_manifest_hash"], r"^sha256:[0-9a-f]{64}$")
            self.assertIn("tests.unit", {item["id"] for item in result["evidence"]["tests"]})
            self.assertIn("build.win", {item["id"] for item in result["evidence"]["builds"]})

    def test_snapshot_check_is_deterministic_and_detects_stale_state(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            manifest = temp / "progress.json"
            toolchain = temp / "toolchain.json"
            manifest.write_text(json.dumps(self.manifest), encoding="utf-8")
            toolchain.write_text("{}\n", encoding="utf-8")
            patches = (mock.patch.object(progress_snapshot, "git_commit", return_value="b" * 40), mock.patch.object(progress_snapshot, "git_dirty", return_value=False))
            with patches[0], patches[1]:
                progress_snapshot.update(manifest, toolchain, temp, generated_at="2026-07-12T20:00:00Z")
                before = manifest.read_bytes()
                self.assertFalse(progress_snapshot.update(manifest, toolchain, temp, check=True))
                self.assertEqual(before, manifest.read_bytes())
            with mock.patch.object(progress_snapshot, "git_commit", return_value="c" * 40), mock.patch.object(progress_snapshot, "git_dirty", return_value=False):
                with self.assertRaisesRegex(progress_snapshot.ManifestError, "commit"):
                    progress_snapshot.update(manifest, toolchain, temp, check=True)

    def test_snapshot_evidence_upsert_is_stable(self):
        existing = [{"id": "tests.unit", "label": "Old", "path": "old.txt"}]
        additions = [{"id": "tests.unit", "label": "Current", "path": "current.txt"}, {"id": "tests.asset", "label": "Asset", "path": "asset.txt"}]
        result = progress_snapshot._merge_evidence(existing, additions)
        self.assertEqual([item["id"] for item in result], ["tests.asset", "tests.unit"])
        self.assertEqual(result[1]["label"], "Current")


if __name__ == "__main__":
    unittest.main()
