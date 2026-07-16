# TASKS

Last Updated: 2026-07-15

## Active Task
Goal: **Editor Live production hardening** (preflight + status model + journal + regression + docs).
Acceptance Criteria: plugin compiles; regression script passes automated cases; contracts updated; hardening report complete.
Relevant Files: `BPPreflight.*`, `BPAgentLiveService.*`, `BPATEdit.*`, `scripts/editor_live_regression.ps1`.
Current Status: C++ done in source; **compile pending** (editor open). Docs/report/script partially done.

## Priority Queue

### P0
- [x] Plugin 0.4.8 + full regression → **14 pass / 0 fail / 0 skip** (reg_20260716_104956)
- [x] Property-aware preflight — **validated** (required→exit20 block, optional→success_with_warnings)
- [x] Editor Live hardening (journal/idempotency/stale_plan/asset_lock/post_analyze) — **validated**
- [x] Automate all 14 regression cases (incl. PIE/compiling via test_control fault-injection,
      editor-exit recovery via recover_scan, save-fail via read-only package)

### P1
- [~] Real business-asset editing: live apply DONE on copy; optional property fixes validated (P18)
- [ ] Update agent_call/edit/create contracts + request_schemas + acceptance checklist for new statuses,
      preflight, stale_plan, and recover_scan/test_control tasks

### P2
- [ ] Apply P18 post-write-crash tolerance to remaining wrappers if they exhibit teardown crashes
      (create + edit done).
- [ ] Optional: `include` toggle already exists for widget settable/bindable arrays; consider size caps for
      very large WBP analyze.

## Recently Completed
- [x] Editor Live production hardening v0.4.8 — preflight (required/optional), stale_plan, journal,
      idempotency, asset_lock, post_analyze, expanded status model, recover_scan (pending_editor_restart),
      test_control fault-injection; **full 14-case regression 14/0/0** (2026-07-16).
- [x] CanvasPanelSlot anchor-aware geometry guard (create/edit/IR/diff) — commit `c5fd197`.
- [x] Widget-tree edit ops on existing WBPs + widget-aware diff — `d7d6d1f`.
- [x] `set_parent_class` / `reparent_blueprint` atomic edit — `86ea1c7`.
- [x] Full WBP spec MVP (handler body templates + graph preview + compare) — `db935a5`.
- [x] Widget event handler exec/data wiring (Phase 4 P2) — `7f4d435`.
- [x] Widget `settable_properties` + alias resolution — `743ddce`.
