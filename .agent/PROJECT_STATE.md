# PROJECT_STATE

Last Updated: 2026-07-15

## Current Goal
**Editor Live production hardening** — property-aware preflight, expanded status model, journal/idempotency,
baseline hash/stale_plan, regression matrix, contract docs. Target: other AIs can call editor_live long-term.

## Current Status
- Done (prior): analyze/create/edit, editor_live validated on AClient, 16-op Settings Graphics apply on copy.
- **Done (this phase):** Editor Live production hardening **compiled to 0.4.8 and FULLY validated** —
  regression **14 pass / 0 fail / 0 skip** (`reg_20260716_104956`). Preflight, stale_plan, idempotency,
  journal, asset_lock, post_analyze, dirty/PIE gates, compiling-wait retry, save-fail, and
  pending_editor_restart recovery all validated. New tasks: recover_scan, test_control (fault-injection).
- **Remaining:** contract doc pass (agent_call/edit/create, request_schemas, acceptance checklist).

## Stable Facts
- Agent version: **0.4.8** (compiled + loaded; status manifest reports plugin_version=0.4.8).
- Build: source plugin at `D:/Projects/AClient/Plugins/BPParserTestGen` is a COPY — run
  `install_project_plugin.ps1` then `build_project_plugin.ps1 -UERoot D:/Projects/AEngine` (editor CLOSED).
- Test project: `E:\BPTestProject\BPTest` (UE 5.4.4). Real: `D:\Projects\AClient` (engine `D:\Projects\AEngine`).
- Generated test copy: `/Game/Generated/WBP_Agent_Live_Settings_Graphics`.
- editor_live status: read `manifest.status` (exit codes are coarse; 10 = partial OR success_with_warnings).

## Important Files (this phase)
- `BPPreflight.h/.cpp` — preflight reports
- `BPAgentLiveService.cpp` — journal, idempotency, asset lock, post_analyze
- `BPATEdit.cpp` — baseline_ir_hash, stale_plan, success_with_warnings
- `scripts/editor_live_regression.ps1`
- `bpparser_testgen/deliverables/reports/editor_live_production_hardening_report.md`

## Latest Known Good Result
- Date: 2026-07-16
- What: Editor Live hardening 0.4.8 compiled + FULL 14-case regression on `/Game/Generated/` copies.
- Result: **14 pass / 0 fail / 0 skip**. Adds cases 06 (PIE refuse), 07 (compiling-wait retry), 09 (concurrent),
  11 (rollback), 12 (save-fail), 13 (post-analyze diff), 14 (recover_scan pending_editor_restart).
- Evidence: `D:/Projects/AClient/Saved/BPParserAgentReports/editor_live_regression/reg_20260716_104956.json`;
  report `bpparser_testgen/deliverables/reports/editor_live_production_hardening_report.md`.

## Open Questions
- [ ] Contract doc pass (agent_call/edit/create, request_schemas, acceptance checklist) for new statuses,
      preflight, stale_plan, and recover_scan/test_control tasks.
- [ ] Cases 06/07 use fault-injection (bounded PIE/busy window); optional occasional real-PIE/real-compile check.
