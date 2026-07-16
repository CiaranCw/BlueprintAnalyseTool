# HANDOFF

Updated: 2026-07-15 (overwrite — do not append)

## Immediate Next Step
Editor Live production hardening (v0.4.8) is **fully validated: regression 14 pass / 0 fail / 0 skip**
(`reg_20260716_104956.json`). ALL 14 cases automated. Remaining work per user: **contract doc pass**
(agent_call/edit/create, request_schemas, external_agent_acceptance_checklist) to document the new statuses,
preflight, stale_plan, and the new tasks (recover_scan / test_control). Editor is currently OPEN.

## What Was Just Done
- **All 14 regression cases automated & green** (plugin 0.4.8). Phase 1 (client-side, no rebuild): 09 concurrent,
  11 failing-op rollback, 12 save-fail (read-only package), 13 post-analyze diff. Phase 2 (C++ 0.4.8):
  - `recover_scan` task + `Start()` scan → case 14 (pending_editor_restart for orphaned journal, no process kill);
  - `test_control` task → cases 06 (inject bounded PIE window → blocked_by_editor_state) & 07 (inject bounded
    busy window → deferred with journal waiting/editor_busy, then completes). Fault-injection, honestly labeled.
- Case 03 (prior) fixed across 3 layers: apply optional-skip, Run out-param status, client prefers manifest.status.

## Evidence to Check
- Final regression: `D:/Projects/AClient/Saved/BPParserAgentReports/editor_live_regression/reg_20260716_104956.json` (14/0/0)
- Hardening report: `bpparser_testgen/deliverables/reports/editor_live_production_hardening_report.md`

## Do Not Repeat
- Do NOT rebuild while editor is open (DLL lock). Opening the editor is MANUAL by the user — STOP and tell them.
- Project plugin at `D:/Projects/AClient/Plugins/BPParserTestGen` is a COPY, not a junction — MUST run
  `install_project_plugin.ps1` after every repo source edit, before building.
- editor_live exit codes are coarse (10 = partial OR success_with_warnings; 50 = stale_plan; 30 may be
  blocked_by_editor_state) — read `manifest.status`, not the exit code, for the precise status.
- Do NOT touch `/Game/Assets/Widget/Settings/` — only `/Game/Generated/`.

## Suggested Next Prompt
"继续把 contract 文档补齐" (agent_call/edit/create, request_schemas, acceptance checklist for new statuses +
recover_scan/test_control tasks)
