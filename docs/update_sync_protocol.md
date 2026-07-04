# Update / Sync Protocol — keep an installed Blueprint Agent current

Once the Blueprint Agent is onboarded into a target project (`Tools/BlueprintAgent/` + managed blocks in
`AGENTS.md`/`CLAUDE.md` + `.cursor`/`.claude` files), the source agent repo keeps evolving. This protocol
keeps the installed copy current **idempotently and non-destructively**: it backs up, refreshes only managed
content, preserves user edits, flags re-warmup when the plugin changed, and never touches blueprint assets.

## 1. Why this exists
An installed copy can silently fall behind the source repo. Without a sync mechanism, a project's AI would
use stale scripts/docs/plugin, or (worse) assume `native_full` is ready after the plugin source changed but
was never rebuilt. This protocol makes "installed but maybe outdated" a first-class, detectable state.

## 2. Version strategy
Both the source repo and the installed copy carry `blueprint_agent.version.json`:
```json
{ "schema_version":"1.0", "agent_version":"0.4.0", "agent_commit":"",
  "build_id":"...", "schema_versions":{...},
  "plugin":{ "name":"BPParserTestGen","version":"0.4.0","requires_rebuild":true,"binary_compatible":false },
  "capabilities":{ "analyze":true,"edit":true,"create":true,"editor_live":true } }
```
`agent_commit` is resolved live from git when blank (source side). The installed copy also records install
state in `Tools/BlueprintAgent/blueprint_agent.manifest.json` (`install{installed_agent_version,
installed_agent_commit, install_mode, source_agent_root, last_update_time, last_update_status,
warmup_required_after_update}`) and a hash baseline in `Tools/BlueprintAgent/.agent_sync/sync_state.json`.

## 3. Copy vs Reference mode
- **copy** (default): the target is self-contained (`Tools/BlueprintAgent/{scripts,docs,plugin}` copied in).
  Portable and team-friendly; must be synced when the source updates (this protocol).
- **reference**: the target only stores a pointer (`source_agent_root`) and its docs/entry point at the
  source's absolute paths — always "latest" but path-dependent and unsuitable for shared/CI environments.
Both are supported; `update` refreshes copied files (copy) or just re-points and re-marks warmup (reference).

## 4. Update flow
1. **check** (read-only) — `check_project_agent_version.ps1` reads both version files, computes
   `is_up_to_date`, `install_mode`, `plugin_source_changed`, `conflicts`, `editor_running`, writes
   `Saved/BPParserAgentReports/update/check_result.json`.
2. **plan** — `update_agent_in_project.ps1` writes `.../update/<ts>/update_plan.json` (actions, changes,
   requires_rebuild/warmup, risk notes). `-DryRun` stops here with a `planned` result.
3. **backup** — the whole `Tools/BlueprintAgent/` and each managed-block host file are copied to
   `.../update/<ts>/backup/` before any change.
4. **refresh managed content** — copy mode mirrors `scripts/docs/plugin` from source (excludes
   Binaries/Intermediate and driver prompts); reference mode refreshes pointers only.
5. **managed blocks** — replaces ONLY the `<!-- BEGIN/END BLUEPRINT-AGENT (managed) -->` block in
   `AGENTS.md`/`CLAUDE.md` (and `GEMINI.md` if present); surrounding user content is untouched.
6. **manifest + version + request templates** — descriptor/version refreshed; only `*.template.json`
   examples are rewritten (user-authored request files are preserved/skipped).
7. **warmup marking** — see §5.
8. **result** — `.../update/<ts>/update_result.json` (status, updated/skipped/conflicts/backups,
   requires_warmup/rebuild, next_actions).

## 5. Plugin changes and re-warmup
Docs/scripts/schema-only updates do **not** require a rebuild (`warmup_required=false`). If the plugin
source changed (`Source/**`, `*.Build.cs`, `*.uplugin`, commandlets, compat layer), the update:
- sets `warmup_required_after_update=true`,
- writes `Saved/BPParserAgentReports/status/capability_state.json` with
  `stage="needs_warmup_after_update", plugin_built=false, warmup_required=true`,
- refuses to let callers assume `native_full` is ready until `warmup` succeeds again (which restores
  `stage="native_ready", plugin_built=true`).
Re-warmup runs only on explicit `-RunWarmupAfterUpdate` (and only when the editor is closed).

## 6. Editor open (no DLL hot-replace)
A loaded plugin DLL cannot be safely hot-replaced. If the UE editor is running:
- docs/scripts/request templates are still refreshed,
- the plugin DLL is **not** replaced; `update_result.editor_live.plugin_reload_required=true`,
- re-warmup is skipped with a note to close the editor first.
Do not rely on Live Coding / Hot Reload for the agent plugin by default.

## 7. Conflict detection
The install/update baseline records a SHA-256 per managed file (and per managed block). On update, files
whose current hash differs from the recorded baseline are reported as `modified_in_target`. Default policy:
- docs/scripts/plugin managed files → **backup_and_replace** (full backup taken; reversible),
- user request files (non-`*.template.json`) → **skip** (never overwritten),
- `AGENTS.md`/`CLAUDE.md`/`GEMINI.md` → **managed block only** (never touches user prose),
- `.uproject` → **not modified** unless `-AllowUProjectEdit` (plugin enable stays a warmup concern).

## 8. Scripts
- `scripts/check_project_agent_version.ps1 -TargetDir <proj> -SourceAgentRoot <repo> [-ProjectUProject ...]`
  → `check_result.json`. Exit: 0 up-to-date, 10 update available, 12 not installed, 30 bad input.
- `scripts/update_agent_in_project.ps1 -TargetDir <proj> -SourceAgentRoot <repo> [-ProjectUProject ...]
  [-Mode copy|reference] [-RunWarmupAfterUpdate] [-AllowUProjectEdit] [-AllowOverwriteManagedFiles]
  [-DryRun] [-Strict]` → `update_plan.json` + `update_result.json`. Exit: 0 success, 10 partial(Strict),
  20 failed, 30 bad input.
- Shared logic: `scripts/agent_sync_lib.ps1` (single source of truth for managed content + version/hash),
  dot-sourced by install/check/update so onboarding and update never drift.

## 9. Via the unified dispatcher
```powershell
# inside the target project:
Tools/BlueprintAgent/scripts/blueprint_agent.ps1 -Task update -SourceAgentRoot "D:/Projects/BlueprintAgent" -ProjectUProject "D:/Projects/AClient/AClient.uproject"
```
or a `request.json`:
```json
{ "schema_version":"1.0","task_type":"update",
  "project": { "uproject":"D:/Projects/AClient/AClient.uproject" },
  "request": { "source_agent_root":"D:/Projects/BlueprintAgent", "mode":"copy", "dry_run":false, "run_warmup_after_update":false } }
```

## 10. How a target-project AI should use this
```text
Blueprint Agent is installed, but it may not be the latest version.
1. Run Tools/BlueprintAgent/scripts/check_project_agent_version.ps1 (read check_result.json).
2. If requires_update -> run update_agent_in_project.ps1 (or task_type "update"), or ask the user.
3. If plugin_source_changed -> status becomes needs_warmup_after_update: do NOT claim native_full is ready
   until warmup succeeds again. Prefer editor_live only if the plugin was rebuilt for the running editor.
```

## 11. Current limitations
- `.uproject` plugin-enable and the built copy under `Plugins/` are handled by `warmup`, not `update`
  (update refreshes only the source copy under `Tools/BlueprintAgent/plugin`).
- Conflict policy is backup+replace for managed files (reversible via the backup dir), not a 3-way merge.
- Reference mode depends on a stable local `source_agent_root` path (unsuitable for shared/CI setups).
- `agent_commit` requires git on the source side to be populated automatically (else it stays blank and
  version comparison falls back to `agent_version`).
