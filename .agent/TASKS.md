# TASKS

Last Updated: 2026-07-10

## Active Task
Goal: (none in flight) — last phase "CanvasPanelSlot anchor-aware geometry guard" completed & accepted.
Acceptance Criteria: n/a
Relevant Files: n/a
Current Status: idle; awaiting next-phase decision (see Priority Queue / `HANDOFF.md`).

## Priority Queue

### P0
- [ ] (none)

### P1
- [ ] Real business-asset editing: run `plan-only`/`dry-run` on a user-chosen real WBP (read-only preview),
      review diff (incl. `geometry_semantics`, semantic slot diff, `risk_notes`) before any apply. Still copy
      to `/Game/Generated/` before `apply-and-verify`.
- [ ] Complex full WBP spec generation (nested Box/Overlay, stretch-anchored panels using Offsets/Anchors).

### P2
- [ ] Apply P18 post-write-crash tolerance to remaining wrappers if they exhibit teardown crashes
      (create + edit done).
- [ ] Optional: `include` toggle already exists for widget settable/bindable arrays; consider size caps for
      very large WBP analyze.

## Recently Completed
- [x] CanvasPanelSlot anchor-aware geometry guard (create/edit/IR/diff) — commit `c5fd197`.
- [x] Widget-tree edit ops on existing WBPs + widget-aware diff — `d7d6d1f`.
- [x] `set_parent_class` / `reparent_blueprint` atomic edit — `86ea1c7`.
- [x] Full WBP spec MVP (handler body templates + graph preview + compare) — `db935a5`.
- [x] Widget event handler exec/data wiring (Phase 4 P2) — `7f4d435`.
- [x] Widget `settable_properties` + alias resolution — `743ddce`.
