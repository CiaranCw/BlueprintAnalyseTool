# domain: plugin_cpp (UE 5.4 C++ editor plugin)

Last Updated: 2026-07-10

## Purpose
The `BPParserTestGen` UE editor plugin does all high-fidelity Blueprint work (dump IR / edit / create /
diff / editor-live). PowerShell orchestrates it; this is where UE-API correctness lives.

## Key Files (source of truth)
Root: `bpparser_testgen/Plugins/BPParserTestGen/`
- `BPParserTestGen.Build.cs` — module deps (UMG, UMGEditor, GraphEditor, EditorSubsystem, …).
- `Source/BPParserTestGen/Private/`:
  - `BPGen.cpp` (+ `Public/BPGen.h`) — the centralized graph-API wrapper (spawn nodes, pins, connect,
    compile/save, pin-type factory). **Reuse these; don't re-derive UE API quirks.**
  - `BPWidgetGen.cpp` (+ `.h`) — UMG helpers: create WBP, construct/add widgets, `SetPropertyFromJson`
    (fuzzy/alias + setter fallback), `ListSettableProperties`, event bind + `EnsureEventHandlerEntry`/
    `WireEventHandlerCall`/`AddHandlerBody`, `CanvasAxisStretched`/`CanvasSlotStretchGuard`/`CanvasSlotGeometrySemantics`.
  - `BPGenIRDumper.cpp` — Blueprint → IR JSON (graphs/nodes/pins/edges + `widget_tree`, `settable_properties`,
    `bindable_events`, `widget_event_bindings`, `slot.geometry_semantics`, `dependencies`).
  - `BPCreate.cpp` — spec-driven create commandlet (Actor/Component/Interface/Widget + full-spec).
  - `BPATEdit.cpp` — atomic edit commandlet (plan/backup/apply/compile/rollback/diff; graph + variable +
    `set_parent_class` + widget-tree ops; `BuildDiff` incl. widget-aware + semantic slot diff).
  - `BPAgentLiveService.cpp` — editor_live file-queue service.
  - `*Commandlet.cpp` — `-run=` entry points (BPParserTestDump/BPATEdit/BPCreate/BPBlueprintDiff/…).

## Key Flow (edit example)
1. `edit_blueprint.ps1` → `-run=BPATEdit`. 2. Load BP (optionally WorkOnCopy). 3. baseline IR.
4. plan + precondition/destructive gate. 5. backup. 6. apply ops (`ApplyOne`). 7. compile.
8. save or rollback (no save). 9. modified IR + `BuildDiff` + previews.

## Important Constraints / Pitfalls
- **Re-sync before build**: editing repo source then building a target compiles the target's *copy*; run
  `scripts/install_project_plugin.ps1` first (bit us twice).
- Node lifecycle order matters (PostPlacedNewNode / AllocateDefaultPins / reference-before-pins) — see
  `docs/issue_patterns.md` P1.
- Handler wiring needs a compile between entry-creation and call-node creation (ADR-005).
- CanvasPanelSlot geometry is anchor-aware (ADR-006).

## Deep refs
`docs/architecture.md`, `docs/atomic_capabilities.md`, `docs/blueprint_ir_schema.md`,
`docs/agent_edit_contract.md`, `docs/agent_create_contract.md`, `docs/issue_patterns.md`.
