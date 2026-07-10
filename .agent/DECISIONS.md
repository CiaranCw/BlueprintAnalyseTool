# DECISIONS

Durable technical decisions (ADR style). One entry per lasting choice. Supersede rather than delete.

## ADR-001: C++ editor plugin + PowerShell orchestration (not pure Python/commandlet-only)
Date: 2026 (early) · Status: Accepted
### Context
Need full, correct Graph/Node/Pin access + write/create/edit that only the UE editor APIs provide.
### Decision
A UE 5.4 C++ editor plugin (`BPParserTestGen`) exposes commandlets (dump/edit/create/diff); PowerShell
scripts orchestrate discovery/build/dispatch and emit machine-readable JSON. Python is only a partial
read-only fallback.
### Consequences
Highest fidelity; requires a per-project build ("warmup"). Cross-tool callers use the PS entry points.
### Evidence
`docs/architecture.md`, `docs/fallback_modes.md`, `scripts/blueprint_agent.ps1`.

## ADR-002: Layered analyze modes with never-silent-fail
Date: 2026 · Status: Accepted
### Decision
`auto → native_full → python_partial → offline`, plus `editor_live`. Always write a manifest; classify
failures; never emit a null-shell "success".
### Consequences
Callable in any environment; honest status. See `docs/fallback_modes.md`, `issue_patterns.md` P13.

## ADR-003: Multi-tool distribution via managed blocks + version/sync
Date: 2026 · Status: Accepted
### Decision
Install/update inject **managed blocks** into a target's `AGENTS.md`/`CLAUDE.md`/`GEMINI.md` (never
overwrite user content), track `blueprint_agent.version.json` + `sync_state.json`, and require a per-project
`warmup` (build) before `native_full`. Idempotent, backup/restore, conflict detection.
### Evidence
`docs/update_sync_protocol.md`, `scripts/agent_sync_lib.ps1`, `scripts/*_agent_in_project.ps1`.

## ADR-004: Reflection-driven widget capabilities (no per-widget special-casing)
Date: 2026 · Status: Accepted
### Decision
Widget property set, `settable_properties` discovery, event binding, and delegate discovery are all
reflection-based (`FProperty`, `FMulticastDelegateProperty`, setter-UFUNCTION fallback). Property names use
alias resolution (exact→case-insensitive→bool `b` prefix→DisplayName→normalized) with a warning on alias.
### Consequences
Works for any native or custom UserWidget. See `docs/widget_blueprint_schema.md`, `issue_patterns.md` P14/P16.

## ADR-005: Widget event handler wiring is two-phase (compile between)
Date: 2026 · Status: Accepted
### Decision
Bind bound-event → ensure handler entry (custom event / function) → **compile** (so the UFunction exists)
→ create/reuse call node → connect exec then data pins → optional body template (`print_string`/`set_text`).
Idempotent. A newly-added widget's variable also needs a compile before binding.
### Evidence
`docs/agent_create_contract.md`, `.../BPWidgetGen.cpp` (`EnsureEventHandlerEntry`/`WireEventHandlerCall`/`AddHandlerBody`),
`issue_patterns.md` P17.

## ADR-006: CanvasPanelSlot geometry is anchor-aware; guard Position/Size on stretch axes
Date: 2026-07-10 · Status: Accepted
### Context
`SetSize`/`SetPosition` are anchor-unaware; on a stretched axis `Offsets` are margins, so `Size`/`Position`
silently corrupted a margin (`ScrollBox_List` Bottom 60→620).
### Decision
Guard `Position`/`Size` on stretched axes (skip + `canvas_slot_stretch_axis_size_warning`) unless
`allow_stretch_axis_size_override=true`; accept explicit `Offsets`/`LayoutData`; emit `slot.geometry_semantics`
in IR; decompose `LayoutData` diffs into semantic components + `risk_notes`.
### Evidence
`docs/widget_blueprint_schema.md`, `issue_patterns.md` P21, `.../BPWidgetGen.cpp` (`CanvasSlot*`).

## ADR-007: Never fake success; trust complete artifacts over process exit code
Date: 2026 · Status: Accepted
### Decision
Wrappers reclassify a post-write engine-teardown crash to `<status>_with_exit_warning` when the artifact is
complete + `status` terminal (create + edit wrappers). Distinguish executed/succeeded vs failed vs pending.
### Evidence
`AGENTS.md` §2, `issue_patterns.md` P18, `scripts/{create,edit}_blueprint.ps1`.

## ADR-008: Unified local agent memory (`.agent/`) + thin tool entries
Date: 2026-07-10 · Status: Accepted
### Decision
One canonical memory library under `.agent/`; tool entries (`AGENTS.md`/`CLAUDE.md`/`GEMINI.md`/Cursor rule)
are thin pointers that never duplicate project facts. Hot/warm/cold layering; source-of-truth priority defined
in `AGENTS.md`.
### Evidence
`.agent/README.md`, `AGENTS.md` §0.
