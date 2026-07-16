# Agent Call Contract — Blueprint Understanding (read-only)

The read/analyze-side contract other agents (Claude Code CLI, Cursor Agent, CI) use to obtain
structured info about an existing Blueprint/Widget/Anim Blueprint in **any** UE project, including
custom/source-built engines. Edit-side contract is `docs/agent_edit_contract.md`.

Strictly **read-only** w.r.t. the user's asset. Always emits `manifest.json`. Never silent-fails.

## 1. Entry point

```powershell
.\scripts\analyze_blueprint.ps1 `
  -ProjectUProject "<...>.uproject" `
  -AssetPath "/Game/UI/WBP_X"           # or a local .uasset path under <project>/Content `
  [-UERoot "<engine root>"]             # optional; auto-resolved from EngineAssociation (version or GUID) `
  [-OutputDir "<dir>"]                  # default: <project>/Saved/BPParserAgentReports `
  [-Mode auto|offline|python-partial|native-full]   # default auto `
  [-AllowPluginInstall] [-AllowBuild]   # gate native-full's invasive steps in a foreign project `
  [-Strict]
```

Exit codes: `0 success` (or partial when not `-Strict`), `10 partial (Strict)`, `20 failed`, `30 bad input`.

## 2. Modes (see docs/fallback_modes.md)

| Mode | UE launch | Builds | Full Graph/Node/Pin/Edge | Use |
|---|---|---|---|---|
| `editor_live` | no (reuses an **open** editor) | no | **yes** (from live memory) | day-to-day, editor already open; no cold start |
| `offline` | no | no | no | project/version/asset-path/uasset-header baseline |
| `python-partial` | yes (target UE + PythonScriptPlugin) | no | no (parent/interfaces/deps/type) | non-invasive partial |
| `native-full` | yes (target UE) | plugin build (gated) | **yes** | complete IR, cold start / CI / editor closed |
| `auto` | as needed | as gated | best available | editor_live → native-full(if feasible/allowed) → python-partial → offline |

`editor_live` reuses an already-open UE editor through the in-editor `BPAgentLiveService` (file queue); it
never launches UnrealEditor-Cmd. Full flow, safety rules, and protocol: `docs/editor_live_mode.md`.

### editor_live task surface (`task_type`)
| task | purpose |
|---|---|
| `status`  | editor state + `editor_live.supports{...}` capability flags (always answers immediately) |
| `analyze` | read-only IR of a loaded asset (this contract) |
| `edit` / `create` | authorised mutation with preflight / stale_plan / journal (`agent_edit_contract.md` / `agent_create_contract.md`) |
| `recover_scan` | flag in-flight requests orphaned by an editor crash (non-terminal journal + no outbox marker) as `pending_editor_restart`; also runs on service `Start()` |
| `test_control` | **regression fault-injection only** — set a bounded, self-expiring PIE/busy window to exercise the gate/retry paths deterministically (does not start real PIE / real compile) |

`status.editor_live.supports` advertises hardening capabilities:
`preflight, stale_plan, request_journal, idempotency, asset_lock, post_analyze, recover_scan, test_control`.
An agent should gate advanced behavior on these flags (and on `plugin_version`) rather than assuming them.

`native-full` requires the read-only dumper plugin (`BPParserTestGen`) present in the target project.
For a foreign project it is copied in (`-AllowPluginInstall`) and built (`-AllowBuild`) against the
target's engine — an opt-in, removable step. Without those flags, `auto` falls back to python-partial/offline.

## 3. Output layout

```
<OutputDir>/<SanitizedAssetPath>/
  manifest.json            # PRIMARY entry for agents
  blueprint_ir.json        # (native_full) full IR ; else partial_ir.json
  summary.md               # human-readable
  understanding_score.json # per-capability scoring + confidence
  graphs/<Graph>.json      # per-graph (native_full)
  viz/blueprint.dot, blueprint.mmd
  logs/warnings.json, errors.json, *_run.txt
```

## 4. manifest.json (how to read)

```json
{ "schema_version":"1.0", "status":"success|partial|failed",
  "mode":"offline_asset_scan|python_partial|native_full",
  "asset_path":"", "asset_name":"", "asset_type":"", "parent_class":"",
  "project_uproject":"", "ue_root":"", "engine_version":"", "is_custom_engine":true,
  "plugin_installed":true, "plugin_built":true, "read_only":true, "generated_at":"",
  "fallbacks_used":[], "outputs":{...}, "counts":{...},
  "warnings":[], "errors":[], "manual_check_required":[] }
```

Interpretation rules for a calling agent:
1. `status`: `success` → full IR (only when `mode==native_full`); `partial` → some info, graph likely absent; `failed` → see `errors`.
2. `mode` + `fallbacks_used` → which layer produced the result and why higher layers were skipped.
3. `counts.nodes/edges==0` with `mode!=native_full` → graph structure NOT captured; call again with `-Mode native-full -AllowPluginInstall -AllowBuild` (after user consent) for the full IR.
4. `manual_check_required` → what a human/native pass must still verify.
5. On `failed`, feed back to the user: `manifest.json`, `logs/errors.json`, `logs/*_run.txt`.

`blueprint_ir.json` (native_full) / `partial_ir.json` (others) share one shape:
`asset{asset_path,asset_type,parent_class,generated_class,implemented_interfaces,dependencies}`,
`blueprint{variables,functions,macros,event_dispatchers,components,graphs}`, `graphs[]` (nodes/pins/edges),
`analysis{manual_check_required,...}`. `summary.md` is for humans; `viz/*.dot|.mmd` render with Graphviz/Mermaid.

## 5. Staying current (installed copies)

An INSTALLED agent (`Tools/BlueprintAgent/`) may lag the source repo. Before relying on it, probe with
`scripts/check_project_agent_version.ps1 -TargetDir <proj> -SourceAgentRoot <repo>` (→ `check_result.json`:
`is_up_to_date`, `plugin_source_changed`, `requires_warmup_after_update`, `conflicts`). If outdated, run
`scripts/update_agent_in_project.ps1` (or `task_type:"update"`). If the plugin source changed, status becomes
`needs_warmup_after_update` — do NOT claim `native_full`/`editor_live` is ready until `warmup` rebuilds it.
Full protocol: `docs/update_sync_protocol.md`.

## 6. Guarantees / non-goals

- Read-only; never compiles/saves the asset; never modifies blueprint content.
- `offline` cannot replace the in-engine dumper; `python-partial` cannot reliably yield Pin/edge structure.
- Full Graph/Node/Pin/Edge IR is only claimed under `native_full` (or `editor_live`, in an open editor).
