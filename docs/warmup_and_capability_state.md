# Warmup & Capability State — how another AI orchestrates per project

This is the flow an external AI (Claude Code / Cursor Agent) follows when it starts working on a
**new project**. It answers: *what stage is this project at, what can I do right now, and do I need a
one-time warmup first?*

## The rule (short)
- **Partial, read-only understanding** (asset type, parent class, interfaces, dependencies): **no warmup**.
- **Full understanding (Graph/Node/Pin/Edge) + Edit + Create**: require a **one-time per-project warmup**
  (install the read-only source plugin + incrementally build the project's Editor target against its
  engine). After warmup, call native_full repeatedly; re-warmup only if the engine or plugin version changes.

Why: full-graph read, edit and create run as **C++ commandlets that must be compiled into the target's
own engine context** (the asset's parent class + custom/plugin K2 nodes are defined there). Python/offline
cannot produce them (proven: AClient WBP via python gives type/parent/deps but no graph).

## State machine (per project × engine × plugin-version)
```
                 no UE engine resolvable
   offline_only  ────────────────────────────►  only offline_asset_scan
        │ (UE found)
        ▼
   needs_install ──(warmup: install plugin)──►  needs_build ──(warmup: build editor)──►  native_ready
        │                                                                                    │
        └── python available? → python_partial usable now (partial)          full understand/edit/create
```
- `offline_only`  : UE not resolvable → only `offline_asset_scan`.
- `python_only`   : UE ok, no native plugin, PythonScriptPlugin present → `offline` + `python_partial` (partial).
- `needs_install` : UE ok, plugin not in project → warmup needed for native.
- `needs_build`   : plugin copied but editor DLL not built → warmup (build) needed.
- `native_ready`  : plugin installed + built → **native_full** → full understand + edit + create.

## Orchestration (recommended call sequence)
1. **Probe (read-only, no UE launch):**
   ```powershell
   .\scripts\blueprint_agent.ps1 -RequestJson status_request.json   # task_type=status
   # or: .\scripts\agent_status.ps1 -ProjectUProject <proj> [-UERoot <engine>]
   ```
   Read `capability_state.json` → `stage`, `available_modes`, `capabilities{understand_full,edit,create}`,
   `warmup_required`, `recommended_action`, `next_calls`.
2. **Decide:**
   - `capabilities.understand_full == true` → go straight to analyze/edit/create in `native_full`.
   - `warmup_required == true` AND you have user consent to build → **warmup once**:
     ```powershell
     .\scripts\warmup_project.ps1 -ProjectUProject <proj> [-UERoot <engine>] [-SmokeAssetPath /Game/...]
     # or task_type=warmup with project.engine_policy.allow_incremental_compile=true
     ```
     Read `warmup_state.json` → `native_full_ready`.
   - No consent / cannot build → use `python_partial` or `offline` (results are `partial`; do NOT treat as full).
3. **Work:** `blueprint_agent.ps1 -RequestJson <analyze|edit|create request>` with `execution.mode=auto`
   (auto self-selects the best available mode) or `native_full`.

`Mode=auto` also self-warms **if** `engine_policy.allow_project_plugin_install` + `allow_incremental_compile`
are true; otherwise it falls back and records `fallbacks_used`. Explicit `status`→`warmup`→`work` is the
clearest sequence for an orchestrating AI.

## capability_state.json (read-only probe output)
```json
{ "schema_version":"1.0","read_only":true,"project_uproject":"","engine_association":"","ue_root":"",
  "engine_version":"5.4.4","is_custom_engine":true,
  "unreal_cmd_present":true,"python_plugin_available":true,
  "plugin_name":"BPParserTestGen","plugin_installed":false,"plugin_built":false,
  "stage":"needs_install","warmup_required":true,
  "available_modes":["offline_asset_scan","python_partial"],
  "capabilities":{"understand_partial":true,"understand_full":false,"edit":false,"create":false},
  "recommended_action":"...","next_calls":["..."] }
```
- `plugin_built` heuristic = `<Project>/Plugins/<Plugin>/Binaries/Win64/UnrealEditor-<Plugin>.dll` exists.
  The definitive test is the native run itself (the analyzer falls back if it actually fails).

## warmup_state.json (after warmup)
```json
{ "schema_version":"1.0","status":"success|partial|failed","native_full_ready":true,
  "steps":[{"step":"install_plugin","status":"success"},{"step":"build_editor","status":"success"},
           {"step":"smoke_native_dump","status":"success"}], "smoke":"success",
  "capability_state":"capability_state.json" }
```

## Source of truth & re-entrancy
- **`capability_state.json` is the single authoritative state** (produced by the read-only probe). If you
  ever fixed something by hand, re-run `agent_status` (or `task_type=status`) to get the true state.
- `warmup_state.json` reflects the outcome of the last full `warmup_project.ps1` run. If a warmup partially
  failed and you fixed it manually, **re-run warmup** (it is idempotent: reinstall + rebuild + reprobe) so
  `warmup_state.json` and `capability_state.json` agree. Do not trust a stale `warmup_state.json` over the
  live probe.
- `warmup` now (a) enables the plugin in the `.uproject` on install, and (b) asserts the module DLL exists
  after build — so a "Build OK" that produced no DLL (plugin not enabled) now fails loudly instead of a
  false success.

## Safety / consent
- `status` is strictly read-only (no UE launch, no changes) — always safe to call first.
- `warmup` is invasive to the **project/toolchain** (adds a source plugin + one incremental build); it never
  modifies blueprint assets. Run only with consent (`engine_policy.allow_incremental_compile=true`).
- Removal: delete `<Project>/Plugins/<Plugin>` (see `docs/integration_guide.md`).

## Verified
- BPTest → `native_ready` (understand_full/edit/create all true).
- AClient (custom engine AEngine 5.4.4) → `needs_install`, python_partial available, `warmup_required=true`.
- `warmup_project.ps1` on BPTest → install+build+smoke all success → `native_full_ready=true` (idempotent).
