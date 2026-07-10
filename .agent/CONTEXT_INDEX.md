# CONTEXT_INDEX

Which files to read for a given kind of task. Read hot files first (`PROJECT_STATE`, `TASKS`,
`HANDOFF`), then only the rows below that match your task. Do not load `logs/` or `archive/` unless
tracing history. Deep references live in `../docs/`.

## Analyze / understand a Blueprint or WBP
- `.agent/domains/analyze_modes.md`, `.agent/domains/plugin_cpp.md`
- docs: `agent_call_contract.md`, `blueprint_ir_schema.md`, `fallback_modes.md`, `editor_live_mode.md`
- source: `scripts/analyze_blueprint.ps1`, `scripts/blueprint_agent.ps1`,
  `bpparser_testgen/.../Private/BPGenIRDumper.cpp`, `.../BPParserTestDumpCommandlet.cpp`

## Create a Blueprint / Widget Blueprint
- `.agent/domains/widget_blueprint.md`, `.agent/domains/plugin_cpp.md`
- docs: `agent_create_contract.md`, `widget_blueprint_schema.md`, `request_schemas.md`
- source: `scripts/create_blueprint.ps1`, `.../Private/BPCreate.cpp`, `.../Private/BPWidgetGen.cpp`

## Edit an existing Blueprint / WBP (atomic, reversible)
- `.agent/domains/widget_blueprint.md` (widget-tree ops), `.agent/domains/plugin_cpp.md`
- docs: `agent_edit_contract.md` (§3b reparent, §3c widget edits), `request_schemas.md`, `readonly_safety.md`
- source: `scripts/edit_blueprint.ps1`, `.../Private/BPATEdit.cpp`, `.../Private/BPWidgetGen.cpp`

## Widget slot geometry / anchors (CanvasPanelSlot)
- `.agent/domains/widget_blueprint.md` → "Anchor-aware geometry"; `DECISIONS.md` ADR-006
- docs: `widget_blueprint_schema.md` ("CanvasPanelSlot anchor-aware geometry editing"), `issue_patterns.md` P21

## Install / update / warmup the agent in a target project
- `.agent/domains/distribution_and_versioning.md`
- docs: `update_sync_protocol.md`, `warmup_and_capability_state.md`, `onboarding_other_ai.md`
- source: `scripts/{install_agent_into_project,update_agent_in_project,warmup_project,check_project_agent_version}.ps1`,
  `scripts/agent_sync_lib.ps1`, `blueprint_agent.version.json`

## editor_live (operate inside a running editor)
- `.agent/domains/analyze_modes.md` → "editor_live"
- docs: `editor_live_mode.md`
- source: `.../Private/BPAgentLiveService.cpp`, `scripts/editor_live_client.ps1`

## Recurring pitfalls (always worth a glance before deep work)
- docs: `issue_patterns.md` (P1–P21). `.agent/HANDOFF.md` → "Do Not Repeat".
