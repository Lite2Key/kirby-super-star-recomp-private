import copy
import json
from pathlib import Path

import jsonschema
import pytest


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / "schemas" / "differential-vector.schema.json").read_text())
VECTORS = json.loads((Path(__file__).with_name("lifted_vectors.json")).read_text())
RESET_VECTORS = json.loads((Path(__file__).with_name("kss_reset_prefix_vectors.json")).read_text())


def test_committed_differential_vectors_are_strict_and_unique():
    for corpus in (VECTORS, RESET_VECTORS):
        jsonschema.Draft202012Validator(SCHEMA).validate(corpus)
    ids = [vector["id"] for corpus in (VECTORS, RESET_VECTORS) for vector in corpus["vectors"]]
    assert len(ids) == len(set(ids))


def test_kss_reset_prefix_is_explicitly_emulator_derived():
    assert RESET_VECTORS["reference"] == {
        "kind": "emulator",
        "name": "MesenCE 2.2.1 S-CPU reset trace",
        "version": "kss-usa-reset-prefix-v1",
    }
    assert len(RESET_VECTORS["vectors"]) == 6


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("extra",), True),
        (("vectors", 0, "initial", "hidden"), 1),
        (("vectors", 2, "writes", 0, "kind"), "opcode"),
        (("vectors", 0, "bytes", 0), 256),
    ],
)
def test_schema_rejects_unknown_or_out_of_range_state(path, value):
    candidate = copy.deepcopy(VECTORS)
    cursor = candidate
    for component in path[:-1]:
        cursor = cursor[component]
    cursor[path[-1]] = value
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.Draft202012Validator(SCHEMA).validate(candidate)
