"""Verifies QueryAPI returns tool_response-shaped dicts even on error."""

from __future__ import annotations
import json
from pathlib import Path

import pytest

from blueprint_agent_tools.query_api import QueryAPI
from blueprint_agent_tools.schema_validator import SchemaValidator


@pytest.fixture
def tiny_layout(tmp_path: Path) -> Path:
    """Build a minimal valid IR cache for /Game/Foo/BP_Empty."""
    proj = tmp_path / "TestProj"
    bp_dir = proj / "blueprints" / "Game__Foo__BP_Empty"
    bp_dir.mkdir(parents=True)

    manifest = {
        "schema_version": "0.1.0",
        "asset": {
            "asset_path":      "/Game/Foo/BP_Empty",
            "package_path":    "/Game/Foo/BP_Empty.BP_Empty",
            "asset_name":      "BP_Empty",
            "blueprint_class": "Blueprint",
            "generated_class": "/Game/Foo/BP_Empty.BP_Empty_C",
            "parent_class":    "/Script/Engine.Actor",
            "blueprint_type":  "BPTYPE_Normal",
            "engine_version":  "5.4.0",
            "parse_time":      "2026-05-19T00:00:00Z",
            "parse_status":    "success",
            "warnings":        [],
            "errors":          []
        },
        "members": {
            "variables": [], "functions": [], "macros": [],
            "event_dispatchers": [], "implemented_interfaces": [],
            "timelines": [], "components": None
        },
        "graphs": [],
        "complexity": {"graph_count": 0, "total_nodes": 0, "total_edges": 0, "max_graph_depth": 0}
    }
    (bp_dir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    return tmp_path


def test_envelope_ok(tiny_layout: Path):
    api = QueryAPI(output_dir=tiny_layout)
    res = api.list_blueprint_assets()
    SchemaValidator().validate("tool_response", res)
    assert res["ok"]
    assert len(res["data"]["assets"]) == 1
    assert res["data"]["assets"][0]["asset_path"] == "/Game/Foo/BP_Empty"


def test_envelope_error_for_missing_manifest(tiny_layout: Path):
    api = QueryAPI(output_dir=tiny_layout)
    res = api.get_blueprint_manifest("/Game/Does/Not/Exist")
    SchemaValidator().validate("tool_response", res)
    assert not res["ok"]
    assert res["errors"][0]["code"] == "E1002"
