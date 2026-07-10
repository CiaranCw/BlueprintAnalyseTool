# HANDOFF

Updated: 2026-07-10  (overwrite this file each session — do not append)

## Immediate Next Step
No task is in flight. Ask the user which P1 to start (see `TASKS.md`): (a) real business-asset
`plan-only` editing, or (b) complex full WBP spec generation. Do not start invasive work unprompted.

## What Was Just Done
- Implemented the CanvasPanelSlot **anchor-aware geometry guard** (create + edit + IR + diff) and fixed
  the silent margin corruption (ScrollBox_List `Offset Bottom` 60→620). Committed `c5fd197`, pushed.
- Re-ran the Existing-WBP edit acceptance on a fresh copy of `WBP_Settings_Graphics`: guard skips the
  bad `Size` op, `unexpected_changes=0`, source untouched. AClient updated to 0.4.6, `native_ready`.

## Current Hypothesis
Understand/edit/create for both regular and Widget Blueprints is feature-complete for the MVP scope.
The tool is ready to be exercised on real business assets in read-only (`plan-only`) mode first.

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
