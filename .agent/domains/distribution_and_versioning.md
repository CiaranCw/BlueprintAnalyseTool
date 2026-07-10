# domain: distribution_and_versioning

Last Updated: 2026-07-10

## Purpose
How the agent is onboarded/updated into a target UE project and how capability/version state is tracked.
Separate from `.agent/` (which is *this repo's* dev memory).

## Model
- `blueprint_agent.version.json` — canonical version + capability flags (source of truth).
- Install/update inject **managed blocks** into a target's `AGENTS.md`/`CLAUDE.md`/`GEMINI.md`
  (BEGIN/END markers; never overwrite user content; tolerate/repair broken blocks). User files backed up.
- `sync_state.json` in the target records install mode (copy/reference), managed-file hashes (conflict
  detection), plugin baseline hash, and warmup flags.
- **warmup** = one-time per-project: sync plugin source into `Plugins/`, build the editor target against the
  project's engine, refresh `Saved/BPParserAgentReports/status/capability_state.json`. Required before
  `native_full`. Changing plugin source → target marked `needs_warmup_after_update`.

## Key Files
- `scripts/agent_sync_lib.ps1` — shared helpers (managed blocks, hashes, descriptor, conflicts, plugin change).
- `scripts/install_agent_into_project.ps1` — onboarding (+ backups, `-NoBackup`).
- `scripts/update_agent_in_project.ps1` — idempotent update (check/plan/backup/refresh/managed-blocks/warmup mark);
  `-RunWarmupAfterUpdate`.
- `scripts/check_project_agent_version.ps1` — reports installed-vs-source status → `check_result.json`.
- `scripts/warmup_project.ps1`, `install_project_plugin.ps1`, `build_project_plugin.ps1`.

## Current State (AClient)
- `D:\Projects\AClient` on agent 0.4.6, `stage=native_ready`, plugin built, `warmup_required=false`.
- Typical dev loop after editing plugin source: `install_project_plugin` → `build_project_plugin` (or
  `update_agent_in_project -RunWarmupAfterUpdate`) → verify `capability_state.json`.

## Pitfalls
- After changing plugin source, a target must re-warmup before `native_full` works.
- `OrderedDictionary.Count` vs array count; managed-block BEGIN-without-END repair (handled in lib).

## Deep refs
`docs/update_sync_protocol.md`, `docs/warmup_and_capability_state.md`, `docs/onboarding_other_ai.md`,
`docs/integration_guide.md`.
