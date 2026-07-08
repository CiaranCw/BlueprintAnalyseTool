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
- **Name resolution (alias matching)**: keys are resolved against the widget/slot class (incl. the inherited chain)
  in this order — exact → case-insensitive → bool `b` prefix (`DefaultChecked`→`bDefaultChecked`) → Details
  DisplayName (`Default Checked`) → strip spaces/underscores. When a non-exact match is used, a
  `property_alias_matched` note is recorded (`input`,`resolved_to`,`widget`) so the caller always knows the real
  field written. **Prefer the internal name** (see the settable_properties section) to avoid relying on aliases.
- **Convenience setters**: if no property matches, the agent calls a single-input setter UFUNCTION `Set<Key>`. This
  makes `CanvasPanelSlot` `Position` / `Size` / `Anchors` / `Alignment` work (setters over `LayoutData`). Box slots
  expose `Padding`/`Size`/`HorizontalAlignment`/`VerticalAlignment` as real properties, so those set directly.
- Unknown key (no property, alias, or `Set<Key>` setter) → `property_not_found` in `property_notes` **with a
  `suggestions` list** (candidate `{name,display_name,type}`) + a `warning` + `manual_check_required` (never silent).

Verified (redumped): `TextBlock.Text`, `Visibility`, `CanvasPanelSlot` Position+Size (→ `LayoutData.Offsets`),
and on custom `WBP_Setting_CheckboxItem`: `TextName` (exact), `DefaultChecked`→`bDefaultChecked` (alias), `NopeXYZ`
(not found → suggestions).

## Widget settable_properties and property name resolution
**Do not guess field names.** The Details panel shows a *DisplayName* (e.g. `Default Checked`), but the real
`FProperty` name can differ (e.g. `bDefaultChecked`) — bool properties commonly carry a `b` prefix. To write
properties reliably, first **analyze** the widget (or the target WBP) and read `settable_properties`.

Every widget node in the IR (and the WBP's own class at the IR root) carries:
```json
"settable_properties": [
  { "name": "bDefaultChecked", "display_name": "Default Checked",
    "type": { "category": "bool", "sub_category": "", "sub_category_object": "", "container_type": "none" },
    "declaring_class": "/Script/AClient.RGSettingsCheckboxItemWidget",
    "editable": true, "blueprint_visible": true, "blueprint_read_only": false, "deprecated": false,
    "current_value": "False", "set_supported": true, "notes": [] }
],
"slot_settable_properties": [ /* editable props of this widget's slot object */ ]
```
- **Scope**: `CPF_Edit` (Details-editable) properties across the **whole inherited chain** — the widget's own C++
  UPROPERTYs, inherited engine props, and a custom `UserWidget`'s exposed variables. Delegates are **excluded**
  (they are in `bindable_events`); functions are excluded; structural backrefs (`Slot`/`Slots`) are excluded.
- **`set_supported`** is `false` (with a `notes` entry) for `readonly_or_internal` (EditConst), `transient`, or
  `deprecated` properties.
- **`current_value`** is the exported text of the property on the live instance (or the class default at IR root).

### Recommended workflow for other AIs
```text
1. analyze the target WBP or custom control;
2. read settable_properties (use `name`, not `display_name`) and slot_settable_properties;
3. build the create/edit request with the real internal names;
4. run create/edit;
5. redump and verify current_value changed.
```
On a wrong name, read `property_notes[].suggestions` (or the redump's settable_properties) and retry with a
`name` from the list.

## Outputs
`create` writes to `<OutputDir>/create/<...>/`:
- `manifest.json` (status/outputs), `create_result.json` — both carry `property_notes[]`
  (`property_alias_matched` / `property_not_found` with `suggestions`) for widget property resolution
- `created_ir.json` — includes `widget_tree` with per-widget `settable_properties`/`slot_settable_properties`
  (see `docs/blueprint_ir_schema.md`)
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

## Widget Event Handler Connection (Phase 4 P2)
The `handler` object controls what the bound-event's exec (and its data params) connect to. Three kinds:

| `handler.type` | behaviour |
|---|---|
| `bound_event` (default) | the bound-event node **is** the entry; no extra node/wiring. |
| `custom_event` | ensure a `Custom Event` named `handler.name` (params mirror the delegate), then route the bound event into it via a self-call node (exec + data). |
| `function` | ensure a function graph `handler.name` (params mirror the delegate), then create a call node and wire exec + data. Pure functions are rejected (`function_is_pure`). |

```json
"events": [
  { "widget": "PlayButton",   "event": "OnClicked",          "handler": { "type": "custom_event", "name": "OnPlayClicked",       "create_if_missing": true, "connect_exec": true, "connect_parameters": true } },
  { "widget": "QualityCombo", "event": "OnSelectionChanged", "handler": { "type": "function",     "name": "HandleQualityChanged", "create_if_missing": true } }
]
```
`create_if_missing` (default `true`), `connect_exec` (default `true`), `connect_parameters` (default `true`). If
`create_if_missing=false` and the handler does not exist → `handler_not_found` (bound event still created; no wiring).

**Wiring order (matters in UE):** bind/find the bound-event node → ensure the handler entry (custom event / function
signature) → **compile** (so the handler UFunction exists) → create/reuse the call node → connect exec → connect data
pins → compile → redump. Data pins are matched **by name (case-insensitive) then by unique type**; a same-type tie is
`ambiguous_parameter_match` (skipped, warned), an incompatible type is `parameter_type_mismatch`, a missing target is
`parameter_pin_missing`. **Idempotent**: the bound-event node, the custom event / function, and the call node are all
reused on repeat, and existing links are never duplicated.

Result (per event) is recorded under `widget_event_bindings[].handler`:
```json
"handler": { "type": "function", "name": "HandleQualityChanged", "connected": true, "exec_connected": true,
  "parameters_connected": [ { "from": "SelectedItem", "to": "SelectedItem", "status": "connected" },
                            { "from": "SelectionType", "to": "SelectionType", "status": "connected" } ] }
```
Handler failure classes (surfaced in `warnings` + `manual_check_required`): `bound_event_missing | handler_not_found |
handler_create_failed | handler_signature_mismatch | function_is_pure | exec_pin_missing | exec_connection_failed`,
plus per-param `parameter_pin_missing | parameter_type_mismatch | ambiguous_parameter_match`.

### Handler body logic template (MVP)
A handler may carry a `body` array of simple logic ops that are generated **inside** the handler entry (the Custom
Event, the function entry, or the bound event), chained by exec. MVP ops:

| op | fields | effect |
|---|---|---|
| `print_string` | `text` OR `from_param` | `KismetSystemLibrary::PrintString`; `from_param` wires the event/handler param (autocast to string). |
| `set_text` | `target` (widget name), `text` OR `from_param` | `<target>.SetText(...)`; `from_param` wires a param (autocast to text). Target must expose `SetText`. |

```json
"handler": { "type": "custom_event", "name": "OnApplyClicked",
  "body": [ { "op": "print_string", "text": "Settings applied" },
            { "op": "set_text", "target": "TitleText", "text": "Applied" } ] }
```
```json
"handler": { "type": "function", "name": "HandleQualityChanged",
  "body": [ { "op": "set_text", "target": "TitleText", "from_param": "SelectedItem" } ] }
```
Each op's exec chains off the previous; `from_param` connects the entry's matching data-out pin (auto-inserting a
conversion node when types differ, e.g. `bool`/`float`→string, `string`→text). Results are recorded in the create-side
`widget_event_bindings[].handler.body_ops[{op,status,detail}]` + `body_applied` (statuses `connected | reused |
param_not_connected | spawn_failed | target_no_settext | unsupported_op`). Idempotent (skips if the entry already
drives a chain). The handler *body business logic beyond these templates* is out of scope (see Deferred).

Verified (UE 5.4, `WBP_Agent_EventHandlerMatrix`): Button.OnClicked→custom_event; CheckBox.OnCheckStateChanged→
custom_event (bool `bIsChecked`); ComboBoxString.OnSelectionChanged→function (`SelectedItem`+`SelectionType` enum);
Slider.OnValueChanged→function (`Value`); EditableTextBox.OnTextChanged→custom_event (`Text`) — all exec+params
connected, idempotent on re-run.

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
- **Rich handler body logic** — MVP body templates (`print_string`, `set_text`; literal or `from_param`) are
  generated inside the handler (see "Handler body logic template"). Anything beyond these simple ops (branches,
  variable math, multi-node business logic) is not auto-generated.
- **Property binding** (`widget.bindings`, e.g. Text→getter) — accepted, not applied.
- **Custom widget internal expansion** — the custom widget is a black box (its own child tree is not recursed).
- **UMG Animation** (`widget.animations`) — accepted, not applied.
- **Pixel-accurate rendered preview** — only the hierarchy diagram is produced (headless render is out of scope).

## Notes / limits
- Slot geometry for `CanvasPanel` is stored in `LayoutData` (redump shows `slot.LayoutData=(Offsets=…)`), driven
  by the `Position`/`Size`/`Anchors`/`Alignment` convenience setters.
- Changing the plugin source (this feature) means any project that had it built must **re-warmup** before
  `native_full`/`editor_live` widget creation is available (status → `needs_warmup_after_update`).
