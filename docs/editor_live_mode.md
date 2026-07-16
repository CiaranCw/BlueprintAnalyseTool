# editor_live Mode — reuse an already-open UE editor

`editor_live` lets external AIs (Claude Code / Cursor / Codex) run **analyze / edit / create** against a UE
editor that is **already open**, without launching a new `UnrealEditor-Cmd` process per request. It is the
hot path for day-to-day blueprint work; `native_full` remains the cold-start / CI path.

Transport (phase 1): a **local file queue** (no ports, no HTTP) served by the in-editor `BPAgentLiveService`
(part of the `BPParserTestGen` editor plugin).

---

## 1. Why editor_live is needed
Large projects take a long time to open in UE. Cold-starting `UnrealEditor-Cmd` for every blueprint analysis
is unacceptable during iterative work. When the developer already has the editor open, the plugin can service
requests in-process in seconds (assets are already loaded), and can even see **unsaved** in-memory edits.

## 2. editor_live vs native_full
| | editor_live | native_full |
|---|---|---|
| UE process | reuses the **open** editor | launches a new `UnrealEditor-Cmd` |
| Cold start | none | seconds–minutes |
| IR completeness | **full** (same unified schema) | **full** |
| Source of truth | live **in-memory** objects (`source_state` records memory vs disk) | disk-saved asset |
| Sees unsaved edits | yes | no |
| Best for | iterative dev with editor open | CI, batch, editor closed |
| Writes assets? | only edit/create (authorised); analyze never | only edit/create; analyze never |

Both produce the **same** `blueprint_ir.json` schema (`docs/blueprint_ir_schema.md`); the mode differences are
carried in the manifest's `editor_live` block, not in the IR shape.

## 3. How to start the live service
The service starts automatically when the project's UE **editor** opens with the `BPParserTestGen` plugin
installed **and built** (do a one-time `warmup` first for a foreign project — see `docs/integration_guide.md`).
It never starts inside a commandlet (so `native_full` runs never race the queue). Manual control from the editor
console: `BPAgent.Live.Start` / `BPAgent.Live.Stop` / `BPAgent.Live.Status`.

## 4. How to confirm the service is running
- In the editor console: `BPAgent.Live.Status` prints a one-line status.
- Externally (no UE launch; fast timeout if closed):
  ```powershell
  .\scripts\blueprint_agent.ps1 -Task status -Mode editor_live -ProjectUProject "<...>.uproject" -TimeoutSeconds 20
  ```
  The status manifest reports `editor_live.available/service_running/supports` and `current_editor_state`
  (`is_pie / is_saving / is_compiling_blueprints / dirty_assets_count`).

## 5. How to submit a request
**Recommended — via the dispatcher** (handles request-id, timeout, PNG/SVG, fallback):
```powershell
.\scripts\blueprint_agent.ps1 -Task analyze -Mode auto -PreferEditorLive `
  -ProjectUProject "<...>.uproject" -AssetPaths "/Game/UI/WBP_MainMenu" -TimeoutSeconds 60
```
**Directly (any language)** — write the payload, then the `.ready` commit marker, then poll `outbox`:
```text
<Project>/Saved/BPParserAgentRequests/inbox/<id>.request.json     # payload
<Project>/Saved/BPParserAgentRequests/inbox/<id>.ready            # commit marker (service ignores half-written payloads)
```
Payload (see `docs/request_schemas.md` for all fields):
```json
{ "schema_version":"1.0","request_id":"req_001","task_type":"analyze","mode":"editor_live",
  "asset_paths":["/Game/UI/WBP_MainMenu"],
  "execution":{"read_only":true,"use_loaded_editor_state":true,"allow_dirty_assets":false,"render_preview":true},
  "output_dir":"D:/AClient/Saved/BPParserAgentReports" }
```

## 6. How to read results
Outputs are written under `<output_dir or Saved/BPParserAgentReports>/editor_live/<id>/`:
```text
manifest.json            # PRIMARY entry for agents (status/mode/counts/editor_live block)
blueprint_ir.json        # full unified IR (same schema as native_full)
summary.md
understanding_score.json
graphs/<Graph>.json
viz/blueprint.dot, viz/blueprint.mmd
viz/blueprint.png, viz/blueprint.svg   # only if Graphviz available (else "" + manual_check_required)
logs/live_service_log.txt, logs/warnings.json, logs/errors.json
```
Completion marker: `<Project>/Saved/BPParserAgentRequests/outbox/<id>.done` (or `.failed`), a small JSON
`{ request_id, exit_code, status, manifest }` pointing at the manifest. Poll for it with a timeout.

## 7. Dirty asset / PIE / compiling handling
- **Dirty (unsaved) target**: analyze proceeds against the **loaded memory** version and stamps
  `editor_live.source_state = loaded_dirty_memory` (vs `loaded_clean_memory` / `disk_saved_asset`), plus a
  warning + `manual_check_required` note. The disk version is **not** force-reloaded (that would discard the
  user's work).
- **PIE** (`is_pie=true`): analyze is allowed and flagged; edit/create are **refused** unless
  `execution.allow_edit_during_pie=true`.
- **Compiling / saving** (`is_compiling_blueprints` / `is_saving`): analyze/edit/create **wait** (bounded retry
  budget) so they never read unstable state; if the editor stays busy past the budget the request **fails**
  with a clear reason instead of hanging. `status` always answers immediately (it reports the busy state).
- **Target open in the asset editor and dirty**: analyze proceeds (with warning); edit/create are **refused**
  unless `execution.require_user_ack=true`.

## 8. edit / create safety policy
- **analyze** is strictly read-only: no save, no compile, no node mutation; only the report dir is written.
- **edit** requires `execution.read_only=false` **and** `execution.allow_edit=true`. It reuses `FBPATEdit`
  (Transaction, baseline IR, edit plan, backup, rollback, compile, save, diff report). Destructive ops still
  require `allow_destructive_edit=true`.
- **create** requires `execution.allow_create=true`. It reuses `FBPCreate` (honours `overwrite_policy`;
  compile+save; emits `created_ir`).
- The user's assets are **never** modified by analyze or status.

### 8.1 Property-aware preflight (v0.4.7+)
Before apply, edit/create can run **`FBPPreflight`** (default `execution.run_preflight=true`):
```text
preflight_report.json      # per-property match_kind + required/optional status
normalized_request.json      # request after normalization hints
capability_snapshot.json     # settable_properties / bindable_events per resolved class
```
- Required property miss → preflight exit **20**, apply **blocked**.
- Optional miss (`optional_properties` or `property_semantics.<name>=optional`) → exit **10**
  (`pass_with_warnings`); apply proceeds; op may still warn at apply time.
- Match kinds: `exact_match`, `alias_match`, `display_name_match`, `property_absent`, `property_read_only`,
  `property_type_mismatch`, `ambiguous_match`.

### 8.2 Baseline hash / stale_plan
- `plan-only` / `dry-run` emit `baseline_ir_hash` (SHA-1 of condensed baseline IR) in `edit_plan.json` and
  `edit_result.json`.
- Apply with `edit.baseline_ir_hash` (or `expected_baseline_ir_hash`): if the live asset changed since planning,
  status **`stale_plan`**, exit **50**, no mutation.

### 8.3 Request journal + idempotency
Each request writes `request_journal.json` phases:
`received → preflight → applying → verifying → success|failed|rolled_back`.
Outbox idempotency: resubmitting the same `request_id` re-points to the prior manifest without re-executing.

### 8.4 Expanded status values
| status | meaning | typical exit |
|---|---|---|
| `success` | all ops + save ok, no warnings | 0 |
| `success_with_warnings` | apply ok but property/optional warnings | 10 |
| `partial` | some ops failed or save partial | 10 |
| `failed` | preflight/compile/save blocked | 20 |
| `stale_plan` | baseline hash mismatch | 50 |
| `blocked_by_editor_state` | PIE/dirty/compile/saving/asset lock | 30 |
| `pending_editor_restart` | journal incomplete after editor crash | n/a (manual) |

Asset lock: concurrent edit/create on the same asset path defers (returns to inbox) instead of racing.

Post-apply **`post_analyze/`** subfolder: full analyze IR of the modified asset for agent verification.

## 9. Failure & fallback
- No editor / plugin not loaded / service stopped → the client **times out** (`-TimeoutSeconds`) and reports
  `unavailable`; stale inbox files are cleaned up.
- `-Mode auto` (or `-PreferEditorLive`): on `unavailable`, the dispatcher **falls back** to
  native_full → python_partial → offline and records `editor_live{fallback_from,fallback_to}` in
  `dispatch_manifest.json`.
- `-Mode editor_live` (explicit): on `unavailable` it returns `editor_live_unavailable` (exit 24) and does
  **not** launch a commandlet (honouring "don't cold-start when the caller asked for live").
- Requests never hang: every wait is bounded by a timeout / retry budget, and every run writes a manifest.

## 10. When you still need native_full
- The editor is closed (CI, batch, headless, automation servers).
- You just changed the plugin's C++ and must rebuild (close editor → build → reopen or use native_full).
- You want a guaranteed disk-state dump independent of any open editor session.

---

## Manifest extensions (§17)
analyze/edit/create manifests add:
```json
"mode": "editor_live",
"editor_live": {
  "available": true, "service_running": true, "request_id": "",
  "source_state": "loaded_clean_memory|loaded_dirty_memory|disk_saved_asset|unknown",
  "editor_state": { "is_pie": false, "is_saving": false, "is_compiling_blueprints": false, "dirty_assets": [] },
  "fallback_from": null, "fallback_to": null
}
```
The dispatcher's `dispatch_manifest.json` adds `editor_live{attempted,available,fallback_from,fallback_to}`
and `requested_mode` alongside the resolved `mode`.

## Components
- C++ (plugin): `BPAgentLiveService.h/.cpp` (queue poll on the GameThread ticker, editor-state/safety gating,
  reuse of `FBPGenIRDumper`/`FBPATEdit`/`FBPCreate`, report + outbox writing). Wired in
  `BPParserTestGenModule` (interactive editor only; console commands `BPAgent.Live.*`).
- Scripts: `scripts/editor_live_client.ps1` (submit/poll/rasterize, never hangs) and the extended
  `scripts/blueprint_agent.ps1` (`-Task/-Mode/-PreferEditorLive/-TimeoutSeconds`, auto chain).

## Validation status (honest)
- **Static-verified now (no UE needed):** the full file-queue protocol and dispatcher logic — submit →
  in-queue processing → manifest → `outbox/.done|.failed` → dispatch — was exercised end-to-end with a mock
  service, and the timeout/cleanup, auto-fallback, and explicit-unavailable (exit 24) paths were confirmed.
  Both PowerShell scripts pass AST parse checks.
- **Pending local UE validation:** compiling `BPParserTestGen` with the new `BPAgentLiveService` and running
  the §16 acceptance in a real editor (Editor open / closed / dirty / PIE). The single most likely
  compile point to verify is the `GCompilingBlueprint` global (declared in `Kismet2/KismetEditorUtilities.h`)
  used for the "is compiling" state; everything else uses long-stable editor APIs. See
  `docs/external_agent_acceptance_checklist.md`.
