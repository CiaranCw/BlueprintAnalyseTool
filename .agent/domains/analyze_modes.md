# domain: analyze_modes

Last Updated: 2026-07-10

## Purpose
How "understand a Blueprint" runs across environments, and the never-silent-fail contract.

## Modes (ADR-002)
- `offline` — no UE launch: project/version/asset-path/uasset-header scan.
- `python_partial` — target UE + PythonScriptPlugin, read-only reflection (no build/plugin).
- `native_full` — our C++ dumper commandlet (`-run=BPParserTestDump`) → full Graph/Node/Pin/Edge IR
  (+ `widget_tree`, `settable_properties`, `bindable_events`, `widget_event_bindings`, `dependencies`,
  `slot.geometry_semantics`). Requires warmup (built plugin).
- `editor_live` — talk to the in-editor `BPAgentLiveService` via a file queue (no editor restart).
- `auto` — offline baseline → native_full if feasible → python_partial → keep best. Two-phase editor_live
  probe (fast status, then real task) to avoid latency when no editor is running.

## Contract
- ALWAYS writes `manifest.json`; strictly read-only w.r.t. the asset.
- Fail for real: parse errors / empty dumps → `failed`, never a null-shell `success` (P13).
- Unified IR (`blueprint_ir.json`) is the transformed shape; raw dumper output is under `native_raw/`.

## Key Files
- source: `scripts/analyze_blueprint.ps1`, `scripts/blueprint_agent.ps1`, `scripts/editor_live_client.ps1`,
  `.../Private/BPGenIRDumper.cpp`, `.../BPParserTestDumpCommandlet.cpp`, `.../BPAgentLiveService.cpp`
- `include` toggles: `widget_settable_properties`, `widget_bindable_events` (default on) to trim large WBPs.

## Deep refs
`docs/fallback_modes.md`, `docs/agent_call_contract.md`, `docs/blueprint_ir_schema.md`, `docs/editor_live_mode.md`.
