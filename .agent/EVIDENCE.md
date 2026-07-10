# EVIDENCE

Index only — paths + short summaries. No pasted logs/source. Big logs live in a project's
`Saved/BPParserAgentReports/` and in `.agent/logs/`; deep references live in `../docs/`.

## Capabilities & versioning
- Type: source · Path: `blueprint_agent.version.json` · Summary: agent 0.4.6 + capability flags
  (analyze/edit/create/editor_live/widget_settable_properties/widget_event_handler_wiring/
  widget_handler_body_template/widget_full_spec_create/blueprint_reparent/widget_tree_edit/
  canvas_slot_anchor_guard). Why: the machine-readable capability contract other tools read.

## Widget create / full-spec MVP
- Type: test result · Path: AClient `Saved/BPParserAgentReports/create_fullspec/` · Date: 2026-07-08
  · Summary: `WBP_Agent_FullSpec_SettingsPanel` — compare match widgets 8/8, events 4/4; handler bodies
  (print_string/set_text) connected. Why: proves single-JSON-spec → interactive WBP.
- Type: doc · Path: `docs/widget_blueprint_schema.md` · Summary: widget create/edit schema, events,
  handler body templates, settable_properties, anchor-aware geometry.

## Widget event handler wiring (Phase 4 P2)
- Type: test result · Path: BPTest `Saved/BPParserAgentReports/create_evtmatrix/` · Date: 2026-07-07
  · Summary: 5-event matrix (Button/CheckBox/ComboBox/Slider/EditableText) → custom_event/function,
  exec+params connected, idempotent. Why: reflection-based handler wiring works generically.

## settable_properties + alias resolution
- Type: test result · Path: AClient `Saved/BPParserAgentReports/analyze_settable/` · Date: 2026-07-06
  · Summary: `WBP_Setting_CheckboxItem` exposes `TextName/ItemId/bDefaultChecked` (real names, display names,
  current values); alias `DefaultChecked→bDefaultChecked` warned. Why: callers use real field names.

## set_parent_class / reparent
- Type: test result · Path: BPTest & AClient reparent reports · Date: 2026-07-08
  · Summary: Actor→Pawn, WBP→C++ `RGUserWidget`, WBP→WBP_C succeed; WBP→Actor → `incompatible_parent_type`
  (unchanged); compile-fail → restore old parent. Why: safe reparent w/ rollback. Doc: `agent_edit_contract.md` §3b.

## Existing WBP edit acceptance (+ anchor guard fix)
- Type: test result · Path: AClient `Saved/BPParserAgentReports/accept_edit_v2b/.../` · Date: 2026-07-10
  · Summary: 10-op combined edit on a `/Game/Generated` copy of `WBP_Settings_Graphics`; widget-tree ops all
  succeed; the bad `ScrollBox_List Size` op is **skipped** by the anchor guard (Offset Bottom stays 60);
  `unexpected_changes=0`; source untouched. Why: edit-existing-WBP works and is safe. Doc: `issue_patterns.md` P21.
- Type: test result · Path: BPTest `Saved/BPParserAgentReports/cg_run1|cg_run2` · Date: 2026-07-10
  · Summary: anchor-guard regression (non-stretch Size ok; stretch-Y Size skipped; explicit Offsets ok;
  override + risk_note). Why: guard behaves across all axis cases.

## Recurring pitfalls
- Type: doc · Path: `docs/issue_patterns.md` · Summary: P1–P21 (node lifecycle, wildcard pins, UTF-8 reading,
  widget event reflection, settable_properties, handler wiring, post-write crash, reparent, widget-tree edit,
  CanvasPanelSlot anchor guard). Why: avoid re-hitting solved problems.

## Commit trail (recent)
- `c5fd197` anchor guard · `d7d6d1f` widget-tree edit · `86ea1c7` reparent · `db935a5` full-spec MVP
  · `7f4d435` handler wiring · `743ddce` settable_properties. (git log for full history.)
