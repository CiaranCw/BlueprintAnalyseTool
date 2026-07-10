# domain: widget_blueprint (UMG create + edit)

Last Updated: 2026-07-10

## Purpose
Everything for creating and editing Widget Blueprints: hierarchy, slots, Details, custom UserWidgets,
events + handlers + body templates, `settable_properties`, and anchor-aware slot geometry.

## Current Understanding (all implemented + verified UE 5.4)
- Create: `asset.blueprint_type="Widget"` + `widget.hierarchy` (recursive `type/name/properties/slot/children`).
  Native short names, `/Script/...`, or custom `/Game/...WBP_X` (normalized to `_C`).
- Details/slot set by reflection; **alias resolution** (exact→case-insensitive→bool `b` prefix→DisplayName→
  normalized) with `property_alias_matched`; misses → `property_not_found` + `suggestions`.
- Events: reflection over `BlueprintAssignable` delegates; `handler.type` = `bound_event|custom_event|function`;
  `handler.body` MVP ops `print_string`/`set_text` (literal or `from_param`). Two-phase wiring (ADR-005).
- Edit (existing WBP): `set_widget_property`, `set_slot_property`, `add_widget`, `remove_widget`(destr),
  `move_widget`(destr), `bind_widget_event`. Widget-aware diff categories in `diff_report`.
- Discovery: every widget in IR carries `settable_properties`, `slot_settable_properties`, `bindable_events`.
  **Analyze first, then use real internal names** (e.g. `Default Checked` → `bDefaultChecked`).

## Anchor-aware geometry (IMPORTANT — ADR-006 / P21)
- `CanvasPanelSlot` on a **stretched** axis (`Anchors.Minimum != Maximum`): `Offsets` are **margins**, not
  Position/Size. Setting `Position`/`Size` there is **guarded** → skipped + `canvas_slot_stretch_axis_size_warning`
  unless `allow_stretch_axis_size_override:true`. Use `Offsets {Left,Top,Right,Bottom}` / `LayoutData` instead.
- IR emits `slot.geometry_semantics` (x/y stretch + per-offset semantic). Edit diff decomposes `LayoutData`
  into semantic components + flags large stretch-axis margin deltas in `risk_notes`.

## Important Files
- source: `.../Private/BPWidgetGen.cpp`, `BPCreate.cpp`, `BPATEdit.cpp`, `BPGenIRDumper.cpp`
- docs: `widget_blueprint_schema.md` (primary), `agent_create_contract.md`, `agent_edit_contract.md §3c`

## Known Issues / Deferred
- Deferred: UMG Animation, pixel-accurate render, custom-widget internal recursion, rich handler-body logic
  beyond print_string/set_text.
- `moved_widgets`/`removed_widgets` diff reflect before→after *state*: a widget added then moved/removed in the
  same batch nets into `added_widgets` (correct, not a miss).

## Open Questions
- [ ] Complex full WBP spec (nested layouts + stretch panels) — use Offsets/Anchors per ADR-006.
