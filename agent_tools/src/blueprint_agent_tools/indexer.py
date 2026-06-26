"""Atomic capability #18. Builds search indices from IR files.

Skeleton — full implementation in M3.
"""

from __future__ import annotations
from pathlib import Path

from .output_layout import OutputLayout


class IndexBuilder:
    """SQLite + JSON index over IR files. Read-once, query-many."""

    def __init__(self, layout: OutputLayout) -> None:
        self.layout = layout
        self.layout.index_dir.mkdir(parents=True, exist_ok=True)

    def build(self, force: bool = False) -> dict:
        """Scan blueprints_dir and (re)build indices.

        Planned outputs:
            _index/assets.sqlite     -- 1 row per blueprint
            _index/nodes.sqlite      -- 1 row per node (FTS over title/comment)
            _index/search.json       -- compact summary for read-only consumers

        Returns: stats dict.
        """
        # TODO(M3): implement using sqlite3 + FTS5.
        return {
            "asset_count": 0,
            "node_count":  0,
            "force":       force,
            "status":      "not_implemented_yet",
        }
