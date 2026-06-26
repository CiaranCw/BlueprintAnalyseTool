"""Atomic capability #20. Agent-facing facade.

Every public method returns a dict matching tool_response.schema.json.
"""

from __future__ import annotations
import time
from pathlib import Path
from typing import Any

from .errors import BPATError, NODE_NOT_FOUND
from .ir_client import IRClient
from .output_layout import OutputLayout, resolve_layout
from .schema_validator import SchemaValidator
from .slicer import Slicer

SCHEMA_VERSION = "0.1.0"


def _envelope(tool: str, fn) -> dict:
    """Wrap a callable so its return value is the standard tool response."""
    started = time.perf_counter()
    try:
        data = fn()
        elapsed = (time.perf_counter() - started) * 1000.0
        return {
            "tool":     tool,
            "ok":       True,
            "data":     data,
            "errors":   [],
            "warnings": [],
            "meta": {
                "from_cache":     True,
                "schema_version": SCHEMA_VERSION,
                "elapsed_ms":     round(elapsed, 3),
                "ue_invoked":     False,
            },
        }
    except BPATError as ex:
        elapsed = (time.perf_counter() - started) * 1000.0
        return {
            "tool":     tool,
            "ok":       False,
            "data":     None,
            "errors":   [ex.to_dict()],
            "warnings": [],
            "meta": {
                "from_cache":     True,
                "schema_version": SCHEMA_VERSION,
                "elapsed_ms":     round(elapsed, 3),
                "ue_invoked":     False,
            },
        }


class QueryAPI:
    def __init__(self, output_dir: str | Path) -> None:
        self.layout: OutputLayout = resolve_layout(output_dir)
        self.ir = IRClient(self.layout)
        self.slicer = Slicer(self.ir)
        self.validator = SchemaValidator()

    # -------- read_only tools ---------------------------------------------

    def list_blueprint_assets(self, filter_class: str | None = None) -> dict:
        def _go():
            assets = self.ir.list_assets()
            if filter_class:
                assets = [a for a in assets if a["blueprint_class"] == filter_class]
            return {"assets": assets}
        return _envelope("list_blueprint_assets", _go)

    def get_blueprint_manifest(self, asset_path: str) -> dict:
        return _envelope("get_blueprint_manifest", lambda: self.ir.get_manifest(asset_path))

    def list_graphs(self, asset_path: str) -> dict:
        return _envelope("list_graphs", lambda: {"graphs": self.ir.list_graphs(asset_path)})

    def get_graph_summary(self, asset_path: str,
                          graph_id: str | None = None,
                          graph_name: str | None = None) -> dict:
        def _go():
            gid = graph_id
            if gid is None and graph_name is not None:
                g = self.ir.find_graph_by_name(asset_path, graph_name)
                if g is None:
                    raise BPATError(NODE_NOT_FOUND, f"graph_name={graph_name} not found",
                                    where={"asset_path": asset_path})
                gid = g["graph_id"]
            if gid is None:
                raise ValueError("graph_id or graph_name required")
            return self.ir.get_graph_summary(asset_path, gid)
        return _envelope("get_graph_summary", _go)

    def get_node_detail(self, asset_path: str, node_id: str) -> dict:
        return _envelope("get_node_detail",
                         lambda: self.ir.get_node_detail(asset_path, node_id))

    def search_nodes(self, asset_path: str, keyword: str,
                     node_class: str | None = None, limit: int = 50) -> dict:
        def _go():
            # TODO(M3): use indexer FTS. For now, do linear scan over graph_full.
            matches: list[dict[str, Any]] = []
            kw = keyword.lower()
            for g in self.ir.list_graphs(asset_path):
                full = self.ir.get_graph_full(asset_path, g["graph_id"])
                if not full:
                    continue
                for n in full.get("nodes", []):
                    if node_class and n.get("node_class") != node_class:
                        continue
                    title = (n.get("node_title") or "").lower()
                    comment = (n.get("node_comment") or "").lower()
                    if kw in title or kw in comment:
                        matches.append({
                            "node_id":    n["node_id"],
                            "node_class": n.get("node_class"),
                            "graph_id":   g["graph_id"],
                            "snippet":    n.get("node_title", ""),
                        })
                        if len(matches) >= limit:
                            return {"matches": matches}
            return {"matches": matches}
        return _envelope("search_nodes", _go)

    def trace_exec_flow(self, asset_path: str,
                        start_node_id: str, max_depth: int = 8) -> dict:
        return _envelope("trace_exec_flow",
                         lambda: self.slicer.trace_exec_flow(asset_path, start_node_id, max_depth))

    def trace_variable_usage(self, asset_path: str, variable_name: str) -> dict:
        return _envelope("trace_variable_usage",
                         lambda: self.slicer.trace_variable_usage(asset_path, variable_name))

    def slice_subgraph(self, asset_path: str, start_node_id: str,
                       direction: str = "forward", depth: int = 8,
                       edge_filter: str = "all") -> dict:
        return _envelope("slice_subgraph",
                         lambda: self.slicer.slice_subgraph(
                             asset_path, start_node_id, direction, depth, edge_filter))

    def validate_blueprint_ir(self, asset_path: str) -> dict:
        def _go():
            mf_path = self.layout.manifest_path(asset_path)
            mf_res = self.validator.validate("manifest", mf_path)
            errors = list(mf_res["errors"])
            for g in self.ir.list_graphs(asset_path):
                gs_path = self.layout.graph_summary_path(asset_path, g["graph_id"])
                if gs_path.is_file():
                    res = self.validator.validate("graph_summary", gs_path)
                    errors += [{"graph_id": g["graph_id"], **e} for e in res["errors"]]
            return {"ok": not errors, "errors": errors, "schema_version": SCHEMA_VERSION}
        return _envelope("validate_blueprint_ir", _go)
