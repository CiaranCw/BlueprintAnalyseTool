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

## 9. Never-hang / always-manifest  `[static]`
- Every dispatcher run writes `dispatch_manifest.json`; every editor_live run writes `manifest.json`.
- Every editor_live wait is bounded by `-TimeoutSeconds`; explicit `editor_live` unavailable → exit 24 with
  no commandlet launched. (All three verified via mock in this repo.)

---

## Current repo status
- `[static]` steps 1, 4-wiring, 9 pass here (mock live-service e2e, timeout/cleanup, auto-fallback, explicit
  exit-24). Both PowerShell scripts pass AST parse checks.
- `[ue]` steps require compiling `BPParserTestGen` (now including `BPAgentLiveService`) into the target
  project and running in a real editor — **pending local UE build/validation**.
