# domain: orchestration_scripts (PowerShell entry points)

Last Updated: 2026-07-10

## Purpose
PowerShell scripts under `scripts/` are the callable surface: UE discovery, plugin install/build,
task dispatch, JSON I/O, validation. Windows PowerShell 5.1; UTF-8 without BOM everywhere.

## Key Files
- `blueprint_agent.ps1` — **unified dispatcher** (`-Task analyze|edit|create|status|warmup|update`,
  `-Mode`, `-AssetPaths`, editor_live two-phase probe, `include` passthrough, asset-path normalization).
- `analyze_blueprint.ps1` — analyze (auto/native-full/python-partial/offline); `Read-JsonUtf8`; carries
  `widget_tree`/`settable_properties`/`widget_event_bindings`/`dependencies` into the unified IR.
- `create_blueprint.ps1` — `-run=BPCreate` wrapper; **P18 crash-tolerance**; emits `viz/graph.dot|.mmd`.
- `edit_blueprint.ps1` — `-run=BPATEdit` wrapper (`-Mode plan-only|dry-run|apply|apply-and-verify`,
  `-CreateBackup`, `-AllowDestructiveEdit`, `-WorkOnCopy`); P18 crash-tolerance.
- `compare_widget_spec.ps1` — expected-vs-actual compare for a create request → `compare_report.json`.
- `cleanup_test_assets.ps1` — deletes ONLY `/Game/Generated/WBP_Agent_*` (dry-run by default; `-Execute`).
- `install_project_plugin.ps1` / `build_project_plugin.ps1` — sync plugin source into a project + build it.
- `warmup_project.ps1`, `install_agent_into_project.ps1`, `update_agent_in_project.ps1`,
  `check_project_agent_version.ps1`, `agent_sync_lib.ps1` — distribution (see distribution_and_versioning.md).
- `editor_live_client.ps1` — client for the in-editor service.

## Conventions / Pitfalls
- Read UTF-8 JSON with `Read-JsonUtf8` / `[IO.File]::ReadAllText(path, UTF8(false))`, never
  `Get-Content -Raw | ConvertFrom-Json` on PS5.1 (P13).
- Pass **absolute** `-SpecFile`/`-EditRequestJson` to commandlets (their CWD is the engine dir).
- PS 5.1 has no `&&`; chain with `;` or separate calls. `git commit` via `-F <file>` (no bash heredoc).
- Editor must be closed for apply/create/warmup.

## Deep refs
`docs/agent_call_contract.md`, `docs/request_schemas.md`, `docs/integration_guide.md`, `docs/fallback_modes.md`.
