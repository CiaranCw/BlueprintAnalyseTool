# External-AI Acceptance Checklist

A concrete, repeatable checklist another AI (or a human) can follow to verify the Blueprint Agent —
especially the `editor_live` mode — is working in a target project. Every step lists the command, the
expected artifact, and the pass criterion. Nothing here fakes success: a step passes only when its artifact
actually exists and says so.

Legend: `[static]` = verifiable without UE (already passing in this repo); `[ue]` = needs the target UE.

---

## 0. Prerequisites
- Target `.uproject` path known; engine resolvable (`EngineAssociation` or `-UERoot`).
- For `editor_live` / `native_full`: plugin installed **and built** (one-time `warmup`). See
  `docs/integration_guide.md` and `docs/warmup_and_capability_state.md`.

## 1. Capability probe (no UE)  `[static]`
```powershell
.\scripts\blueprint_agent.ps1 -Task status -Mode auto -ProjectUProject "<...>.uproject"
```
- Artifact: `.../status/capability_state.json`.
- Pass: `stage` + `available_modes` reported; no exceptions.

## 2. editor_live availability probe  `[ue]`
Open the project's UE editor, then:
```powershell
.\scripts\blueprint_agent.ps1 -Task status -Mode editor_live -ProjectUProject "<...>.uproject" -TimeoutSeconds 20
```
- Artifact: `.../editor_live/<id>/manifest.json` with `editor_live.available=true`, `service_running=true`.
- Pass: returns within timeout; `current_editor_state` present. (Editor console `BPAgent.Live.Status` agrees.)

## 3. Editor OPEN — analyze a real blueprint  `[ue]`  (success criteria §18.1–7)
```powershell
.\scripts\blueprint_agent.ps1 -Task analyze -Mode auto -PreferEditorLive `
  -ProjectUProject "<...>.uproject" -AssetPaths "/Game/UI/WBP_MainMenu" -TimeoutSeconds 60
```
- Pass:
  1. `dispatch_manifest.json` → `mode=editor_live`, `editor_live.available=true`, `fallback_from=null`.
  2. `editor_live/<id>/manifest.json` → `status=success`, full `counts` (graphs/nodes/pins/edges > 0).
  3. `blueprint_ir.json`, `summary.md`, `understanding_score.json`, `viz/blueprint.dot|.mmd` exist.
  4. **No new `UnrealEditor-Cmd.exe`** was spawned (check Task Manager / process list).
  5. The blueprint asset is **unchanged** on disk (mtime unchanged; not marked dirty by us).

## 4. Editor CLOSED — auto fallback to native_full  `[ue]`  (§18.8)
Close the editor, then run the same command as step 3.
- Pass: `dispatch_manifest.json` → `editor_live.attempted=true, available=false`,
  `fallback_from=editor_live`, `fallback_to=native_full`; sub-manifest produced by native_full.  `[static]`
  The fallback wiring itself is already verified without UE (mock-unavailable → native path).

## 5. Dirty blueprint  `[ue]`  (§16.3)
Open a blueprint, make an unsaved change, then analyze it via editor_live.
- Pass: manifest `editor_live.source_state=loaded_dirty_memory` + a warning/`manual_check_required` note.
  An `edit`/`create` on that dirty, editor-open target is **refused** unless `require_user_ack=true`.

## 6. PIE  `[ue]`  (§16.4)
Enter Play-In-Editor, then submit analyze and edit.
- Pass: analyze manifest `editor_live.editor_state.is_pie=true` (analysis still of the editor asset, not the
  PIE instance); `edit`/`create` **refused** unless `allow_edit_during_pie=true`.

## 7. Compiling / saving  `[ue]`
Submit a request while a blueprint compile / SaveAll is in progress.
- Pass: request **waits** (bounded) then either succeeds once idle or **fails with a reason** — never hangs;
  `status` still answers immediately with `is_compiling_blueprints`/`is_saving` true.

## 8. edit via editor_live (authorised)  `[ue]`
```powershell
.\scripts\blueprint_agent.ps1 -RequestJson ".\requests\edit_live.json" -Mode editor_live -PreferEditorLive
```
with `execution.read_only=false, allow_edit=true, create_backup=true`.
- Pass: `edit_result.json` / `diff_report.json` produced; backup exists; compile+save succeeded (or clean
  `rolled_back` on failure). Asset changed **only** as planned.

## 8b. Production-hardening matrix (editor_live 0.4.8)  `[ue]`
One script runs the full 14-case matrix on `/Game/Generated/` copies (business assets untouched):
```powershell
.\scripts\editor_live_regression.ps1 -ProjectUProject "<...>.uproject" -HardeningVersion 0.4.7
```
- Artifact: `.../editor_live_regression/reg_<ts>.json` (`passed/failed/skipped` + per-case rows).
- Pass: **14 passed / 0 failed / 0 skipped**. Covers: analyze; preflight required-block (exit 20) & optional
  → `success_with_warnings` (exit 10); alias match; dirty policy; **PIE refuse** `blocked_by_editor_state`;
  **compiling-wait** deferred (journal `waiting/editor_busy`) then completes; idempotent duplicate `request_id`;
  concurrent same-asset; **stale_plan** (exit 50); failing-op **rollback** (exit 40) + journal; **save-fail**
  (read-only package) → `partial` + `save_status=failed`; **post-analyze** diff reflects change; **editor-exit
  recovery** via `recover_scan` → orphan flagged `pending_editor_restart`.
- Note: cases 06 (PIE) and 07 (compiling) use the `test_control` fault-injection task (bounded, self-expiring
  editor-state window) to exercise the real gate/retry code paths without driving a real PIE session / long
  compile in the open editor. It is honestly labeled as injected in each manifest `note`.
- Reference run (AClient, UE 5.4.4, plugin 0.4.8): `reg_20260716_104956.json` — 14/0/0.

## 9. Never-hang / always-manifest  `[static]`
- Every dispatcher run writes `dispatch_manifest.json`; every editor_live run writes `manifest.json`.
- Every editor_live wait is bounded by `-TimeoutSeconds`; explicit `editor_live` unavailable → exit 24 with
  no commandlet launched. (All three verified via mock in this repo.)

---

## Current repo status
- `[static]` steps 1, 4-wiring, 9 pass here (mock live-service e2e, timeout/cleanup, auto-fallback, explicit
  exit-24). All PowerShell scripts pass AST parse checks.
- `[ue]` **validated on AClient (UE 5.4.4, plugin 0.4.8):** editor_live status/analyze/edit/create plus the
  full §8b 14-case hardening matrix → **14 passed / 0 failed / 0 skipped**
  (`Saved/BPParserAgentReports/editor_live_regression/reg_20260716_104956.json`). See
  `bpparser_testgen/deliverables/reports/editor_live_production_hardening_report.md`.
