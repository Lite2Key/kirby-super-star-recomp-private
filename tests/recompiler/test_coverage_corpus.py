import copy

import pytest

from recompiler.kssrecomp.coverage_corpus import CoverageCorpusError, merge_coverages
from recompiler.kssrecomp.trace_cfg import build_trace_seeded_cfg


MODE = {"emulation": True, "m8": True, "x8": True}


def capture(extra_pc=None):
    blocks = [
        {"processor": "scpu", "pc": 0x008000, "mode": MODE,
         "hits": 1, "first_cycle": 0, "last_cycle": 0},
        {"processor": "sa1", "pc": 0x008100, "mode": MODE,
         "hits": 1, "first_cycle": 1, "last_cycle": 1},
    ]
    edges = []
    if extra_pc is not None:
        extra = {"processor": "scpu", "pc": extra_pc, "mode": MODE,
                 "hits": 2, "first_cycle": 2, "last_cycle": 3}
        blocks.append(extra)
        edges.append({"source": blocks[0] | {}, "target": extra | {}, "hits": 1})
        for item in edges:
            for endpoint in ("source", "target"):
                for key in ("hits", "first_cycle", "last_cycle"):
                    item[endpoint].pop(key, None)
    return {
        "schema_version": 1,
        "source_format": "mesen-ce-kss-trace-v1",
        "capture": {"bounded": True, "limit": 8, "event_count": sum(x["hits"] for x in blocks), "end_reason": "complete"},
        "processors": {
            "scpu": {"events": sum(x["hits"] for x in blocks if x["processor"] == "scpu"),
                     "unique_blocks": sum(x["processor"] == "scpu" for x in blocks)},
            "sa1": {"events": 1, "unique_blocks": 1},
        },
        "blocks": blocks,
        "edges": edges,
    }


def test_union_retains_route_provenance_and_strict_cfg_validity():
    merged = merge_coverages((('boot', capture()), ('title', capture(0x008001))))
    assert merged["capture"]["routes"] == ["boot", "title"]
    assert len(merged["blocks"]) == 3
    shared = next(item for item in merged["blocks"] if item["pc"] == 0x008000)
    assert shared["hits"] == 2 and shared["routes"] == ["boot", "title"]
    assert merged["processors"]["scpu"] == {"events": 4, "unique_blocks": 2}
    assert len(build_trace_seeded_cfg(merged)["cfg"]["blocks"]) == 3


def test_corpus_rejects_routes_with_different_reset_entries():
    changed = copy.deepcopy(capture())
    changed["blocks"][0]["pc"] = 0x008010
    with pytest.raises(CoverageCorpusError, match="reset entries"):
        merge_coverages((('boot', capture()), ('bad', changed)))


def test_corpus_rejects_duplicate_route_ids():
    with pytest.raises(CoverageCorpusError, match="duplicate route"):
        merge_coverages((('boot', capture()), ('boot', capture())))


def continuation(anchor_scpu=0x008000):
    value = capture(0x008002)
    value["blocks"] = [item for item in value["blocks"] if item["processor"] == "scpu"]
    value["blocks"][0]["pc"] = 0x008001
    value["blocks"][1]["pc"] = 0x008002
    value["edges"][0]["source"]["pc"] = 0x008001
    value["edges"][0]["target"]["pc"] = 0x008002
    value["processors"] = {
        "scpu": {"events": 3, "unique_blocks": 2},
        "sa1": {"events": 0, "unique_blocks": 0},
    }
    value["capture"]["event_count"] = 3
    value["capture"]["route_segment"] = {
        "contract": "first-end-frame-to-first-visible-v1",
        "kind": "continuation", "start_event": "first_end_frame",
        "stop_event": "first_visible_frame", "frames_elapsed": 3,
        "continuation_from": [
            {"processor": "scpu", "pc": anchor_scpu, "mode": MODE, "cycle": 10},
            {"processor": "sa1", "pc": 0x008100, "mode": MODE, "cycle": 8},
        ],
        "visible_frame": {"width": 256, "height": 239, "pixel_count": 61184, "nonblack_pixels": 1},
    }
    return value


def test_anchored_continuation_extends_reset_corpus_with_route_provenance():
    merged = merge_coverages((('boot', capture()), ('first-visible', continuation())))
    new = next(item for item in merged["blocks"] if item["pc"] == 0x008002)
    assert new["routes"] == ["first-visible"]
    bridge = next(item for item in merged["edges"] if item["source"]["pc"] == 0x008000)
    assert bridge["target"]["pc"] == 0x008001
    assert bridge["routes"] == ["first-visible"]
    assert merged["capture"]["segments"][1]["kind"] == "continuation"
    assert len(build_trace_seeded_cfg(merged)["cfg"]["blocks"]) == 4


def test_continuation_rejects_unknown_anchor_and_cannot_start_corpus():
    with pytest.raises(CoverageCorpusError, match="cannot start"):
        merge_coverages((('first-visible', continuation()),))
    with pytest.raises(CoverageCorpusError, match="anchor is absent"):
        merge_coverages((('boot', capture()), ('bad', continuation(0x009999))))
