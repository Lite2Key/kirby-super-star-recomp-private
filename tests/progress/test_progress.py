import copy
import importlib.util
import json
import tempfile
import unittest
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
            self.assertEqual(json.loads((out / "progress.json").read_text(encoding="utf-8"))["schema_version"], 1)

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
