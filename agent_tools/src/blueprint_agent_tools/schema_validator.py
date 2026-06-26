"""JSON Schema validation. Atomic capability #21."""

from __future__ import annotations
import json
from pathlib import Path

import jsonschema

_SCHEMAS_DIR = Path(__file__).resolve().parents[2] / "schemas"

_SCHEMA_FILES = {
    "tool_response":      "tool_response.schema.json",
    "manifest":           "blueprint_manifest.schema.json",
    "graph_summary":      "graph_summary.schema.json",
    "node_detail":        "node_detail.schema.json",
    "subgraph_slice":     "subgraph_slice.schema.json",
    "blueprint_ir":       "blueprint_ir.schema.json",
    "error_codes":        "error_codes.schema.json",
}


class SchemaValidator:
    def __init__(self, schemas_dir: Path | None = None) -> None:
        self.schemas_dir = Path(schemas_dir) if schemas_dir else _SCHEMAS_DIR
        self._cache: dict[str, dict] = {}

    def _load(self, name: str) -> dict:
        if name in self._cache:
            return self._cache[name]
        if name not in _SCHEMA_FILES:
            raise KeyError(f"Unknown schema: {name}")
        with (self.schemas_dir / _SCHEMA_FILES[name]).open("r", encoding="utf-8") as f:
            schema = json.load(f)
        self._cache[name] = schema
        return schema

    def validate(self, schema_name: str, payload_path_or_obj) -> dict:
        """Return {ok, errors[]}."""
        schema = self._load(schema_name)

        if isinstance(payload_path_or_obj, (str, Path)):
            with open(payload_path_or_obj, "r", encoding="utf-8") as f:
                payload = json.load(f)
        else:
            payload = payload_path_or_obj

        validator = jsonschema.Draft7Validator(schema)
        errors = []
        for err in validator.iter_errors(payload):
            errors.append({
                "path": list(err.absolute_path),
                "message": err.message,
            })
        return {"ok": not errors, "errors": errors}
