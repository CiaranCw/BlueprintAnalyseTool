"""Read-only safety regression. See docs/readonly_safety.md L8.

Usage:
    python verify_readonly.py snapshot <project_path> --out before.json
    # ... run dump ...
    python verify_readonly.py snapshot <project_path> --out after.json
    python verify_readonly.py diff before.json after.json
"""

from __future__ import annotations
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

# Directories under <project> we EXPECT to be unchanged after a dump.
WATCHED = ("Content", "Source", "Config", "Plugins")


def snapshot(project_path: Path) -> dict:
    out: dict = {"project": str(project_path), "files": {}}
    for sub in WATCHED:
        root = project_path / sub
        if not root.is_dir():
            continue
        for p in root.rglob("*"):
            if not p.is_file():
                continue
            try:
                stat = p.stat()
                rel = str(p.relative_to(project_path)).replace("\\", "/")
                out["files"][rel] = {
                    "size":  stat.st_size,
                    "mtime": int(stat.st_mtime),
                }
            except OSError:
                pass
    return out


def diff(before: dict, after: dict) -> dict:
    bf = before.get("files", {})
    af = after.get("files", {})
    bk, ak = set(bf), set(af)

    added   = sorted(ak - bk)
    removed = sorted(bk - ak)
    changed = sorted(k for k in bk & ak if bf[k] != af[k])

    return {"added": added, "removed": removed, "changed": changed}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sp_snap = sub.add_parser("snapshot")
    sp_snap.add_argument("project_path")
    sp_snap.add_argument("--out", required=True)

    sp_diff = sub.add_parser("diff")
    sp_diff.add_argument("before")
    sp_diff.add_argument("after")

    args = ap.parse_args(argv)

    if args.cmd == "snapshot":
        snap = snapshot(Path(args.project_path).resolve())
        Path(args.out).write_text(json.dumps(snap, indent=2), encoding="utf-8")
        return 0

    before = json.loads(Path(args.before).read_text(encoding="utf-8"))
    after  = json.loads(Path(args.after ).read_text(encoding="utf-8"))
    d = diff(before, after)
    print(json.dumps(d, indent=2, ensure_ascii=False))
    if d["added"] or d["removed"] or d["changed"]:
        print("FAIL: project files changed.", file=sys.stderr)
        return 40
    print("OK: project files unchanged.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
