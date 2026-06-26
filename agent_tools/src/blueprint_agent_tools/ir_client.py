"""Read-only client for IR files produced by BPATIRSerializer."""

from __future__ import annotations
import json
from pathlib import Path

from .errors import BPATError, IR_NOT_DUMPED, NODE_NOT_FOUND
from .output_layout import OutputLayout


class IRClient:
    def __init__(self, layout: OutputLayout) -> None:
        self.layout = layout

    def _read_json(self, path: Path) -> dict:
        if not path.is_file():
            raise BPATError(IR_NOT_DUMPED,
                            f"Expected IR file missing: {path}",
                            where={"path": str(path)})
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)

    def list_assets(self) -> list[dict]:
        out = []
        if not self.layout.blueprints_dir.is_dir():
            return out
        for asset_dir in self.layout.blueprints_dir.iterdir():
            if not asset_dir.is_dir():
                continue
            mf = asset_dir / "manifest.json"
            if not mf.is_file():
                continue
            with mf.open("r", encoding="utf-8") as f:
                m = json.load(f)
            out.append({
                "asset_path":      m["asset"]["asset_path"],
                "blueprint_class": m["asset"]["blueprint_class"],
                "parent_class":    m["asset"]["parent_class"],
                "parse_status":    m["asset"]["parse_status"],
                "complexity":      m.get("complexity", {}),
            })
        return out

    def get_manifest(self, asset_path: str) -> dict:
        return self._read_json(self.layout.manifest_path(asset_path))

    def list_graphs(self, asset_path: str) -> list[dict]:
        return self.get_manifest(asset_path).get("graphs", [])

    def get_graph_summary(self, asset_path: str, graph_id: str) -> dict:
        return self._read_json(self.layout.graph_summary_path(asset_path, graph_id))

    def get_graph_full(self, asset_path: str, graph_id: str) -> dict | None:
        p = self.layout.graph_full_path(asset_path, graph_id)
        return self._read_json(p) if p.is_file() else None

    def get_node_detail(self, asset_path: str, node_id: str) -> dict:
        p = self.layout.node_detail_path(asset_path, node_id)
        if p.is_file():
            return self._read_json(p)
        for g in self.list_graphs(asset_path):
            full = self.get_graph_full(asset_path, g["graph_id"])
            if not full:
                continue
            for n in full.get("nodes", []):
                if n.get("node_id") == node_id:
                    return n
        raise BPATError(NODE_NOT_FOUND,
                        f"node_id={node_id} not in {asset_path}",
                        where={"asset_path": asset_path, "node_id": node_id})

    def find_graph_by_name(self, asset_path: str, graph_name: str) -> dict | None:
        for g in self.list_graphs(asset_path):
            if g["graph_name"] == graph_name:
                return g
        return None
