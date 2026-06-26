"""Smoke tests for SchemaValidator."""

from __future__ import annotations
from pathlib import Path

import pytest

from blueprint_agent_tools.schema_validator import SchemaValidator


def _minimal_manifest() -> dict:
    return {
        "schema_version": "0.1.0",
        "asset": {
            "asset_path":      "/Game/BP_Hero",
            "package_path":    "/Game/BP_Hero.BP_Hero",
            "asset_name":      "BP_Hero",
            "blueprint_class": "Blueprint",
            "generated_class": "/Game/BP_Hero.BP_Hero_C",
            "parent_class":    "/Script/Engine.Character",
            "blueprint_type":  "BPTYPE_Normal",
            "engine_version":  "5.4.0",
            "parse_time":      "2026-05-19T00:00:00Z",
            "parse_status":    "success",
            "warnings":        [],
            "errors":          []
        },
        "members": {
            "variables": [],
            "functions": [],
            "macros": [],
            "event_dispatchers": [],
            "implemented_interfaces": [],
            "timelines": [],
            "components": None
        },
        "graphs": [],
        "complexity": {
            "graph_count": 0,
            "total_nodes": 0,
            "total_edges": 0,
            "max_graph_depth": 0
        }
    }


def test_manifest_minimal_passes():
    v = SchemaValidator()
    res = v.validate("manifest", _minimal_manifest())
    assert res["ok"], res["errors"]


def test_manifest_missing_required_fails():
    v = SchemaValidator()
    bad = _minimal_manifest()
    del bad["asset"]["asset_path"]
    res = v.validate("manifest", bad)
    assert not res["ok"]


def test_tool_response_shape():
    v = SchemaValidator()
    payload = {
        "tool":     "list_blueprint_assets",
        "ok":       True,
        "data":     {"assets": []},
        "errors":   [],
        "warnings": [],
        "meta": {
            "from_cache":     True,
            "schema_version": "0.1.0",
            "elapsed_ms":     1.0,
            "ue_invoked":     False
        }
    }
    res = v.validate("tool_response", payload)
    assert res["ok"], res["errors"]
