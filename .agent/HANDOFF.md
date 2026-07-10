# HANDOFF

Updated: 2026-07-10  (overwrite this file each session — do not append)

## Immediate Next Step
A **plan-only** edit preview for the real `WBP_Settings_Graphics` was produced (16 ops, read-only, asset
untouched). Wait for the user to review it. If approved, apply on a `/Game/Generated/` COPY (delete any
stale copy first) with `apply-and-verify` + backup; the reparent op requires first creating a compatible
base `WBP_Agent_SettingsGraphicsBase` (parent = `/Script/AClient.RGSettingsGraphicsWidget`), else it will
compile-fail and roll back. Do not apply to the real asset.

## What Was Just Done
- Built the unified `.agent/` memory system + thin tool entries (commit `bce5735`).
- Ran read-only `native_full` analyze + `plan-only` edit preview on `WBP_Settings_Graphics` (AClient):
  EN super-res labels, add one of each Setting Item type, a bind_widget_event EventGraph edit, an
  anchor-safe Offsets spacing change, and a (risk-flagged) reparent. Real asset NOT modified.
  Reports: AClient `Saved/BPParserAgentReports/planonly_analyze` and `planonly_edit`.
- (Earlier) CanvasPanelSlot anchor guard (`c5fd197`); AClient at 0.4.6, `native_ready`.

## Current Hypothesis
Understand/edit/create is MVP-complete. The plan is safe to apply on a copy once the compatible reparent
base exists; the reparent is the only high-risk op (must derive from the original C++ parent).

## Evidence to Check
- Anchor guard: `docs/issue_patterns.md` P21; `EVIDENCE.md` → "CanvasPanelSlot anchor guard".
- Acceptance: AClient `Saved/BPParserAgentReports/accept_edit_v2b/.../{edit_result,diff_report}.json`.
- Capabilities/version: `blueprint_agent.version.json`.

## Do Not Repeat
- Do NOT set CanvasPanelSlot `Position`/`Size` on a stretched axis (min≠max) — it overwrites a margin.
  Use `Offsets`/`LayoutData`, or `allow_stretch_axis_size_override:true`. Read `slot.geometry_semantics` first.
- Do NOT edit repo plugin source and then build a target project without re-syncing
  (`scripts/install_project_plugin.ps1`) — the target compiles its own copy (this bit us before).
- Do NOT use `Get-Content -Raw | ConvertFrom-Json` for UTF-8 JSON on PS 5.1 — use `Read-JsonUtf8`
  / `[IO.File]::ReadAllText(..., UTF8)` (see issue_patterns P13).
- Do NOT re-`WorkOnCopy` over an existing `/Game/Generated` copy — DuplicateAsset fails; delete first.
- Do NOT judge create/edit success purely by process exit code (post-write teardown crashes happen;
  trust the complete artifact + `status`, see P18).

## Suggested Next Prompt
"Pick a real WBP in AClient; run `blueprint_agent.ps1` analyze (read-only), then an edit `plan-only`
preview of a small change; show me the diff (with geometry_semantics + risk_notes) before any apply."
