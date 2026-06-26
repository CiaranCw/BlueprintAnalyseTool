"""Build IR indices over a previously dumped OutputDir.

Usage:
    python build_index.py <output_dir> [--force]
"""

from __future__ import annotations
import argparse
import sys
from pathlib import Path

from blueprint_agent_tools.indexer import IndexBuilder
from blueprint_agent_tools.output_layout import resolve_layout


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("output_dir")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)

    layout = resolve_layout(args.output_dir)
    stats = IndexBuilder(layout).build(force=args.force)
    print(stats)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
