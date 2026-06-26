"""Atomic capability #19. Subgraph slicing.

Forward / backward / both BFS over IR edges with edge_kind filter.
Skeleton — full implementation in M3.
"""

from __future__ import annotations
from pathlib import Path

from .errors import BPATError, NODE_NOT_FOUND, SLICE_DEPTH_EXCEEDED
from .ir_client import IRClient

MAX_SLICE_DEPTH = 32


class Slicer:
    def __init__(self, ir: IRClient) -> None:
        self.ir = ir

    def slice_subgraph(
        self,
        asset_path: str,
        start_node_id: str,
        direction: str = "forward",
        depth: int = 8,
        edge_filter: str = "all",
    ) -> dict:
        if depth < 0 or depth > MAX_SLICE_DEPTH:
            raise BPATError(SLICE_DEPTH_EXCEEDED,
                            f"depth must be in [0,{MAX_SLICE_DEPTH}]",
                            where={"depth": depth})
        if direction not in {"forward", "backward", "both"}:
            raise ValueError(f"bad direction: {direction}")
        if edge_filter not in {"exec", "data", "all"}:
            raise ValueError(f"bad edge_filter: {edge_filter}")

        # TODO(M3): implement BFS over union of all graph_full edges.
        return {
            "schema_version": "0.1.0",
            "slice_kind":     "slice_subgraph",
            "origin":         {"asset_path": asset_path, "start_node_id": start_node_id},
            "params":         {"direction": direction, "depth": depth, "edge_filter": edge_filter},
            "nodes":          [],
            "edges":          [],
            "boundary":       {"truncated_at_depth": [], "external_references": []},
            "stats":          {"node_count": 0, "edge_count": 0, "elapsed_ms": 0},
        }

    def trace_exec_flow(self, asset_path: str, start_node_id: str, max_depth: int = 8) -> dict:
        return self.slice_subgraph(asset_path, start_node_id, "forward", max_depth, "exec")

    def trace_variable_usage(self, asset_path: str, variable_name: str) -> dict:
        # TODO(M3): scan all graph_full files for variable_reference matching name
        return {
            "schema_version": "0.1.0",
            "variable":       variable_name,
            "reads":          [],
            "writes":         [],
        }
