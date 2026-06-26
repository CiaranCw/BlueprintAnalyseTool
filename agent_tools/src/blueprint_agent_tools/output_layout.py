"""Resolves OutputDir layout produced by the UE-side BPATIRSerializer.

Layout (see docs/architecture.md §4):
    <output_dir>/<project_name>/
        _summary.json
        _index/
        logs/
        blueprints/<safe_path>/
            manifest.json
            graphs/<id>.summary.json
            graphs/<id>.full.json
            nodes/<id>.json
            slices/<hash>.json
"""

from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path


def make_safe_path(asset_path: str) -> str:
    """/Game/Foo/BP_Bar  ->  Game__Foo__BP_Bar"""
    s = asset_path.lstrip("/")
    return s.replace("/", "__")


@dataclass(frozen=True)
class OutputLayout:
    output_dir: Path
    project_name: str

    @property
    def project_dir(self) -> Path:
        return self.output_dir / self.project_name

    @property
    def index_dir(self) -> Path:
        return self.project_dir / "_index"

    @property
    def logs_dir(self) -> Path:
        return self.project_dir / "logs"

    @property
    def blueprints_dir(self) -> Path:
        return self.project_dir / "blueprints"

    @property
    def summary_path(self) -> Path:
        return self.project_dir / "_summary.json"

    def asset_dir(self, asset_path: str) -> Path:
        return self.blueprints_dir / make_safe_path(asset_path)

    def manifest_path(self, asset_path: str) -> Path:
        return self.asset_dir(asset_path) / "manifest.json"

    def graph_summary_path(self, asset_path: str, graph_id: str) -> Path:
        return self.asset_dir(asset_path) / "graphs" / f"{graph_id}.summary.json"

    def graph_full_path(self, asset_path: str, graph_id: str) -> Path:
        return self.asset_dir(asset_path) / "graphs" / f"{graph_id}.full.json"

    def node_detail_path(self, asset_path: str, node_id: str) -> Path:
        return self.asset_dir(asset_path) / "nodes" / f"{node_id}.json"

    def slice_dir(self, asset_path: str) -> Path:
        return self.asset_dir(asset_path) / "slices"


def resolve_layout(output_dir: str | Path) -> OutputLayout:
    """Pick the single project subdir under output_dir.

    For now we assume one project per OutputDir. If a user mixes multiple
    projects, the caller must pass the full <output_dir>/<project_name>.
    """
    p = Path(output_dir).resolve()
    if (p / "_summary.json").exists() or (p / "blueprints").is_dir():
        return OutputLayout(output_dir=p.parent, project_name=p.name)

    children = [c for c in p.iterdir() if c.is_dir() and (c / "blueprints").is_dir()]
    if len(children) == 1:
        return OutputLayout(output_dir=p, project_name=children[0].name)

    raise FileNotFoundError(
        f"Could not auto-detect project layout under {p}. "
        f"Pass <output_dir>/<project_name> directly."
    )
