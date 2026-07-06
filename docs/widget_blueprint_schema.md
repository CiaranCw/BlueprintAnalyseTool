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

## Events (Phase 4 — partial, supported)
Bind a widget delegate (e.g. Button `OnClicked`) to a graph event. `widget.events`:
```json
"events": [ { "widget": "PlayButton", "event": "OnClicked", "handler": { "type": "custom_event", "name": "OnPlayClicked" } } ]
```
The agent compiles once (so the widget variable exists), then creates a `UK2Node_ComponentBoundEvent` in the
EventGraph — the same "On Clicked (PlayButton)" event node the UMG Details "+ event" button produces (verified:
redump shows it under `graphs[EventGraph]`). **Currently the bound-event node is created but not auto-wired to a
named `handler`** (that wiring is a later refinement, recorded as `manual_check_required`). `is_variable` widgets
are required (the agent sets this automatically).

## Deferred (will warn / `manual_check_required`)
- **Handler wiring** — connecting a bound event's exec to a specific custom event/function by `handler.name`.
- **Property binding** (`widget.bindings`, e.g. Text→getter) — accepted, not applied.
- **Custom `UserWidget` children** — resolution/insertion of project `.._C` widgets.
- **UMG Animation** (`widget.animations`) — accepted, not applied.
- **Pixel-accurate rendered preview** — only the hierarchy diagram is produced (headless render is out of scope).

## Notes / limits
- Slot geometry for `CanvasPanel` is stored in `LayoutData` (redump shows `slot.LayoutData=(Offsets=…)`), driven
  by the `Position`/`Size`/`Anchors`/`Alignment` convenience setters.
- Changing the plugin source (this feature) means any project that had it built must **re-warmup** before
  `native_full`/`editor_live` widget creation is available (status → `needs_warmup_after_update`).
