# Widget Blueprint Creation Schema (Phase 1-3)

How to create a UMG Widget Blueprint from a structured request. Implemented and **verified on UE 5.4** (asset
factory + WidgetTree + slots + Details via reflection + Widget IR + hierarchy preview). Scope is Phase 1-3;
Phase 4+ features are listed under "Deferred".

## Entry
Same unified `create` task (see `docs/agent_create_contract.md` / `docs/request_schemas.md`), with
`asset.blueprint_type = "Widget"` and a `widget.hierarchy` section:

```json
{
  "task_type": "create",
  "asset": {
    "asset_path": "/Game/Generated/WBP_AgentMenu",
    "blueprint_type": "Widget",
    "parent_class": "/Script/UMG.UserWidget",
    "overwrite_policy": "fail_if_exists|create_unique_name|overwrite_if_allowed"
  },
  "widget": {
    "hierarchy": {
      "root": {
        "type": "CanvasPanel",
        "name": "RootCanvas",
        "children": [
          { "type": "TextBlock", "name": "TitleText",
            "properties": { "Text": "Main Menu", "Visibility": "HitTestInvisible" },
            "slot": { "type": "CanvasPanelSlot", "properties": { "Position": {"X":40,"Y":40}, "Size": {"X":400,"Y":60} } } },
          { "type": "Button", "name": "PlayButton",
            "slot": { "type": "CanvasPanelSlot", "properties": { "Position": {"X":40,"Y":120}, "Size": {"X":200,"Y":48} } } }
        ]
      }
    }
  },
  "variables": [], "functions": [], "graphs": [],
  "validation": { "compile": true, "save": true, "redump_ir": true, "render_preview": true }
}
```
`widget.hierarchy` may be the root node directly, or a `{ "root": <node> }` wrapper (both accepted).
`variables/functions/event_dispatchers/graphs` reuse the normal Blueprint create semantics (a WBP is a Blueprint).

## Hierarchy node (recursive)
| field | meaning |
|---|---|
| `type` | builtin short name (`CanvasPanel`,`TextBlock`,`Button`,`Image`,`VerticalBox`,`HorizontalBox`,`Overlay`,…) resolved under `/Script/UMG.`, OR a full class path (`/Script/UMG.Image`). Custom `UserWidget` (a `.._C` path) is **deferred** (see below). |
| `name` | the widget's `FName`; becomes a named/variable widget (`is_variable=true`). |
| `properties` | Details written by reflection (see below). |
| `slot` | how this widget sits in its parent: `{ "type": <slot class>, "properties": { ... } }`. The parent's class determines the real slot class; `type` is advisory. |
| `children` | only valid when this widget is a `UPanelWidget` (Canvas/VerticalBox/…). |

## Property model (widget Details and slot)
Properties are applied by reflection, so **any real `FProperty` name works**:
- Value mapping uses `FJsonObjectConverter`: numbers/bools/strings as-is; `FText` from string; enums by name
  (e.g. `Visibility: "HitTestInvisible"`); structs as nested objects (`FMargin`, `FVector2D`, `FLinearColor`);
  object/asset refs as path strings.
- **Convenience setters**: if a key is not a direct property, the agent calls a single-input setter UFUNCTION
  `Set<Key>`. This makes `CanvasPanelSlot` `Position` / `Size` / `Anchors` / `Alignment` work (they are setters
  over `LayoutData`, not properties). Box slots expose `Padding`/`Size`/`HorizontalAlignment`/`VerticalAlignment`
  as real properties, so those set directly.
- Unknown key (no property and no `Set<Key>` setter) → recorded as a `warning` (never fatal, never silent).

Verified examples (redumped and confirmed): `TextBlock.Text`, `Visibility`, `CanvasPanelSlot` Position+Size
(→ `LayoutData.Offsets`).

## Outputs
`create` writes to `<OutputDir>/create/<...>/`:
- `manifest.json` (status/outputs), `create_result.json`
- `created_ir.json` — includes `widget_tree` (see `docs/blueprint_ir_schema.md`)
- `summary.md`
- `viz/hierarchy.dot`, `viz/hierarchy.mmd` (widget hierarchy; rasterize to PNG/SVG via the client/Graphviz)

## Verification / acceptance
The created WBP opens in the UE editor; its Hierarchy panel shows the widgets; `created_ir.json.widget_tree`
mirrors the hierarchy with each widget's `class`, `is_variable`, `slot.class`, `slot.properties`, and changed
`properties` (redump values align with the request). Blueprint assets other than the created one are not modified.

## Events (Phase 4 — generalized, reflection-based)
Bind ANY widget's BlueprintAssignable multicast delegate to a graph event — **no per-widget special-casing**.
The mechanism is purely reflective: `widget instance → UClass → enumerate FMulticastDelegateProperty → match
event name → UK2Node_ComponentBoundEvent`.

```json
"events": [
  { "widget": "PlayButton",     "event": "OnClicked",          "handler": { "type": "bound_event", "name": "OnPlayClicked" } },
  { "widget": "EnableCheckBox", "event": "OnCheckStateChanged" },
  { "widget": "QualityComboBox","event": "OnSelectionChanged" }
]
```
The agent compiles once (so widget variables exist), resolves the delegate (exact then case-insensitive over the
bindable set), and creates the bound-event node — the same "On X (Widget)" event the UMG Details "+ event" button
produces. Each result is recorded in `manifest.json`/`create_result.json` under `widget_event_bindings`
(`widget/widget_class/event/delegate_property/node_title/node_class/graph/parameters/status/reused`), and the
redumped nodes appear in `created_ir.json.widget_event_bindings`.

Event discovery (no binding) — every widget in the IR carries `bindable_events` (name + parameters); analyze can
request `include.widget_bindable_events` to enumerate an existing WBP's events.

`status` values: `bound | reused` (idempotent — a second identical request reuses the node) | classified failures
`widget_not_found | not_variable | property_missing | delegate_not_found | pins_incomplete | error` (all surfaced in
`warnings` + `manual_check_required`, never silent). `delegate_not_found` also lists the widget's available events.

### Verified capability matrix (UE 5.4)
| Widget class | Event bound | Parameters (redumped) |
|---|---|---|
| Button | OnClicked | (none) |
| CheckBox | OnCheckStateChanged | bIsChecked |
| ComboBoxString | OnSelectionChanged | SelectedItem, SelectionType (`ESelectInfo`) |
| Slider | OnValueChanged | Value |
| EditableTextBox | OnTextChanged | Text |
| SpinBox | OnValueChanged | InValue |
| ScrollBox | OnUserScrolled | CurrentOffset |

Also discoverable/bindable by the same reflection (not all separately verified): Button OnPressed/OnReleased/
OnHovered/OnUnhovered, ComboBox OnOpening, Slider capture-begin/end, EditableText(Box) OnTextCommitted, SpinBox
OnValueCommitted/OnBeginSliderMovement/OnEndSliderMovement, and **custom UserWidget** BlueprintAssignable delegates.

## Custom UserWidget Support (Phase 5)
Reference a project's own `UserWidget` as a child by its class/asset path in `hierarchy.type`. The custom widget
is treated as a **black box** (its internals are not expanded): the agent loads its class, constructs it into the
tree, sets its slot + exposed Details, discovers its events, and records it as a dependency.

**Accepted `type` forms** (all normalized to the `_C` generated class):
```text
/Game/UI/Common/WBP_CustomButton.WBP_CustomButton_C   (generated class path)
/Game/UI/Common/WBP_CustomButton.WBP_CustomButton     (object path)
/Game/UI/Common/WBP_CustomButton                       (package path)
```
Resolution order: load the given path as a `UClass`; else derive `<Pkg>.<Short>_C`; else load the Blueprint and
take its `GeneratedClass`. The class must be a `UWidget` subclass and non-abstract.

**Details / Slot**: set by the same reflection path as native widgets (basic types, enum, object/soft-object by
asset path, `FLinearColor`/`FMargin`/`FVector2D` structs, and the `Set<Key>` setter fallback). Missing property →
`warning`; type mismatch → the importer error is surfaced (never silent).

**Events**: the custom widget's own `BlueprintAssignable` multicast delegates are discovered
(`bindable_events`) and can be bound exactly like native widgets (verified: `WBP_Setting_CheckboxItem` →
`OnCheckboxItemChanged`, `OnVisibilityChanged`).

**Dependency record** (in `created_ir.json` / `manifest.json` / `create_result.json`):
```json
"dependencies": [
  { "type": "custom_user_widget", "asset_path": "/Game/UI/Common/WBP_CustomButton",
    "generated_class": "/Game/UI/Common/WBP_CustomButton.WBP_CustomButton_C" }
]
```
**Failure categories** (surfaced in warnings + manual_check_required): `class_path_invalid | class_load_failed |
not_user_widget | construct_widget_failed | parent_not_panel | property_set_failed`.

Verified (UE 5.4, AClient real project): `/Game/Assets/Widget/Settings/WBP_Setting_CheckboxItem_C` inserted into a
new WBP — class loaded, constructed, slot geometry applied, dependency recorded, events discovered, compile/save OK.

## Deferred (will warn / `manual_check_required`)
- **Handler exec-wiring (P2)** — for `handler.type` `custom_event`/`function`, connecting the bound event's exec to
  a named custom event / function. The bound-event node is created; the wiring is recorded as
  `manual_check_required`. `bound_event` (default) needs no wiring (the node is the entry).
- **Property binding** (`widget.bindings`, e.g. Text→getter) — accepted, not applied.
- **Custom widget internal expansion** — the custom widget is a black box (its own child tree is not recursed).
- **UMG Animation** (`widget.animations`) — accepted, not applied.
- **Pixel-accurate rendered preview** — only the hierarchy diagram is produced (headless render is out of scope).

## Notes / limits
- Slot geometry for `CanvasPanel` is stored in `LayoutData` (redump shows `slot.LayoutData=(Offsets=…)`), driven
  by the `Position`/`Size`/`Anchors`/`Alignment` convenience setters.
- Changing the plugin source (this feature) means any project that had it built must **re-warmup** before
  `native_full`/`editor_live` widget creation is available (status → `needs_warmup_after_update`).
