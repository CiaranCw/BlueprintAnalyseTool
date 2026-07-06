# Research: From Blueprint Parsing to Widget Blueprint Creation

Scope: can the Blueprint Agent grow from "parse existing blueprints" to "build complete Blueprints / Widget
Blueprints from a structured JSON request", callable by other AIs? This is a **research + design** report,
grounded in the current code — not a claim that it already works. Verified facts are marked accordingly.

Verification basis (read from source this session):
- `FBPCreate::Run` creates **Actor / ActorComponent / Interface** blueprints; `blueprint_type=widget` is an
  **explicit hard failure** (`BPCreate.cpp:144`). It supports variables, components (SCS attach = manual note),
  function **signatures** (bodies not auto-wired), event dispatchers, and EventGraph nodes/edges for a small
  node set; then compile + save + dump.
- The plugin `Build.cs` has **no `UMG` / `UMGEditor`** dependency; there is no widget-aware code.
- `FBPGenIRDumper` dumps graphs/nodes/pins/variables/functions/macros/dispatchers/interfaces. It does **not**
  traverse `UWidgetTree` — so a WBP's **visual hierarchy, slots, and Details are invisible** to the current IR
  (analyze of a real WBP returns its event/function graphs only, e.g. WBP_Settings_Graphics = 8 graphs/204 nodes,
  but zero widget-tree structure).

Net: full graph/node/pin/edge parsing is solid and reusable; **all widget-specific capability is missing** and
must be built. A WBP *is* a `UBlueprint`, so the existing K2 graph/variable/function machinery is reusable once
the WBP asset exists — the new work is the UMG layer (asset factory, WidgetTree, slots, Details, bindings,
animations) plus a **Widget IR** extension for verification and preview.

---

## 1. Current capability vs. "create a complete Widget Blueprint"

| Capability | Status | Notes |
|---|---|---|
| Create Actor Blueprint | ✅ have | `FBPCreate` (Actor/Component/Interface), compile+save, verified |
| Create Widget Blueprint | ❌ missing | explicit failure; needs `UWidgetBlueprintFactory` + UMG modules |
| Create/maintain WidgetTree / Hierarchy | ❌ missing | needs `UWidgetTree::ConstructWidget` + panel `AddChild` |
| Add / remove / rename / move widget | ❌ missing | panel slot operations; reparenting |
| Set widget Details (Text/Brush/Visibility/Color/Padding/Alignment/Anchors/Size/RenderTransform) | ❌ missing | reflection-based `FProperty` import; brush/text/struct nontrivial |
| Set Slot props (Canvas/HorizontalBox/VerticalBox/Overlay…) | ❌ missing | slot object created by `AddChild`; per-slot setters or reflection |
| Add project custom `UserWidget` | ❌ missing | resolve `.._C` generated class; dependency capture |
| Bind widget event (Button `OnClicked`) | ❌ missing | `UK2Node_ComponentBoundEvent` bound to widget multicast delegate |
| Property binding (e.g. Text→getter) | ❌ missing | `WBP->Bindings` (`FDelegateEditorBinding`) — distinct from event binding |
| Variables / functions / dispatchers / graph logic | ⚠️ reusable | K2 machinery works on a WBP once it exists; function **bodies** still not auto-wired today |
| UMG Animation | ❌ missing | `UWidgetAnimation` + MovieScene; highest complexity |
| Compile / save / redump / verify | ⚠️ partial | compile+save+dump reusable; **redump must be extended** to emit Widget IR |
| Preview image | ⚠️ partial | graph DOT works; **widget-hierarchy diagram** is easy+new; **pixel-accurate UMG render is hard headless** |

---

## 2. Recommended implementation architecture

1. **New module deps** in `BPParserTestGen.Build.cs`: `UMG` (runtime: `UWidget`/`UWidgetTree`/panels/slots),
   `UMGEditor` (editor: `UWidgetBlueprint`, `UWidgetBlueprintFactory`, widget bp utils). If animations are
   pursued later: `MovieScene` (+ `MovieSceneTools`).
2. **New C++ helper `FBPWidgetGen`** (widget-aware, mirrors `FBPGen`): create WBP asset; construct/add/remove/
   move/rename widgets; set widget & slot properties via reflection; add custom `UserWidget`; create bound
   events; (later) animations. Keep it a thin, reusable wrapper with structured warnings.
3. **Extend `FBPGenIRDumper` with a Widget IR** section (`widget_tree`, `bindings`, `animations`) so BOTH
   analyze (understand existing WBP layout) AND create-verification (redump → compare expected/actual) work.
   This is required for `redump_ir` / `compare_expected_vs_actual` to mean anything for widgets.
4. **Reuse the dispatcher + commandlets**: extend `FBPCreate::Run` to branch on `blueprint_type=widget` into
   `FBPWidgetGen`; add widget operations to `FBPATEdit` (`add_widget`, `set_widget_property`, …). No new
   commandlet needed (BPCreate/BPATEdit already load JSON + run). PS wrappers unchanged except a widget
   preview-render step.
5. **Preview**: generate a **WidgetTree hierarchy diagram** (DOT/Mermaid → PNG) as the primary visual (reliable,
   headless). A true rendered UMG thumbnail is a **stretch goal** (needs off-screen RHI; not guaranteed under
   `-nullrhi`) — see risks.

---

## 3. Unified create request JSON schema (proposed)

Keep the existing unified envelope; add a `widget` section and `events`. `task_type` stays `create`
(`blueprint_type:"Widget"`); accept `create_widget_blueprint` as an alias.

```json
{
  "schema_version": "1.0",
  "task_type": "create",
  "asset": {
    "asset_path": "/Game/Generated/WBP_AgentMenu",
    "blueprint_type": "Widget",
    "parent_class": "/Script/UMG.UserWidget",
    "overwrite_policy": "fail_if_exists|create_unique_name|overwrite_if_allowed"
  },
  "widget": {
    "hierarchy": { "...": "see §5 (recursive widget node)" },
    "bindings":  [ { "widget": "HealthText", "property": "Text", "function": "GetHealthText" } ],
    "animations": [ { "name": "FadeIn", "note": "phase-6+; may be manual_check_required" } ]
  },
  "variables": [ { "name": "Health", "type": { "category": "int" }, "editable": true } ],
  "functions": [ { "name": "GetHealthText", "pure": true, "outputs": [ { "name": "Text", "type": { "category": "text" } } ] } ],
  "event_dispatchers": [],
  "events": [
    { "widget": "PlayButton", "event": "OnClicked",
      "handler": { "type": "custom_event", "name": "OnPlayClicked" } }
  ],
  "graphs": [ { "graph_name": "EventGraph", "nodes": [], "edges": [] } ],
  "validation": { "compile": true, "save": true, "redump_ir": true, "render_preview": true,
                  "expected_ir": "/optional/path/expected.json" }
}
```

- `variables/functions/event_dispatchers/graphs` reuse the **existing** create semantics (a WBP is a Blueprint).
- `widget.hierarchy` and `events` are the **new** UMG parts.
- `validation` drives compile/save/redump/preview + optional expected-vs-actual compare.

---

## 4. Widget Hierarchy data structure (recursive node)

```json
{
  "type": "CanvasPanel",                       // builtin short name, OR full custom class path (see §7)
  "name": "RootCanvas",
  "is_variable": true,                          // expose as a named widget/variable in the graph
  "properties": {                               // Details panel props (reflection; see §6)
    "Visibility": "Visible"
  },
  "slot": {                                     // how THIS widget sits in its parent (parent decides slot class)
    "type": "CanvasPanelSlot",
    "properties": {
      "Anchors":   { "Minimum": [0,0], "Maximum": [1,1] },
      "Offsets":   { "Left": 0, "Top": 0, "Right": 0, "Bottom": 0 },
      "Alignment": [0.5, 0.5],
      "ZOrder": 0
    }
  },
  "children": [
    { "type": "TextBlock", "name": "Title",
      "properties": { "Text": "Main Menu", "ColorAndOpacity": { "SpecifiedColor": [1,1,1,1] } },
      "slot": { "type": "CanvasPanelSlot", "properties": { "Position": [40,40], "Size": [400,60] } } },
    { "type": "Button", "name": "PlayButton",
      "slot": { "type": "CanvasPanelSlot", "properties": { "Position": [40,120], "Size": [200,48] } },
      "children": [ { "type": "TextBlock", "name": "PlayLabel", "properties": { "Text": "Play" } } ] }
  ]
}
```

Rules: the **parent's** class determines the legal `slot.type` (Canvas→`CanvasPanelSlot`, HorizontalBox→
`HorizontalBoxSlot`, …); the agent validates/derives it. `children` only valid on `UPanelWidget` subclasses.
`name` becomes the widget's `FName` and (if `is_variable`) a graph-accessible variable.

---

## 5. Widget IR (dumper extension, for analyze + verification)

`blueprint_ir.json` gains a `widget_tree` mirroring the live `UWidgetTree`:

```json
"widget_tree": {
  "root": {
    "name": "RootCanvas", "class": "/Script/UMG.CanvasPanel", "is_variable": true,
    "slot": { "class": "/Script/UMG.CanvasPanelSlot", "properties": { "...": "..." } },
    "properties": { "Visibility": "Visible" },
    "children": [ { "name": "Title", "class": "/Script/UMG.TextBlock", "properties": { "Text": "Main Menu" }, "slot": {...}, "children": [] } ]
  }
},
"widget_bindings": [ { "widget": "HealthText", "property": "Text", "function": "GetHealthText" } ],
"widget_animations": [ { "name": "FadeIn", "duration": 0.3 } ]
```

Same schema for analyze and for `redump_ir` after create → enables `compare_expected_vs_actual`.

---

## 6. Widget / Slot property representation

Properties are set by **reflection** (`FProperty` on the widget/slot), so any exposed property is addressable
without per-type code. JSON→property mapping via `FJsonObjectConverter::JsonValueToUProperty` /
`FProperty::ImportText_Direct`, with these conventions:

| UE type | JSON representation |
|---|---|
| numeric / bool / string / FName | JSON number / bool / string |
| `FText` | string (localization keys are a manual/optional extension) |
| enum (e.g. `ESlateVisibility`) | enum name string (`"Visible"`, `"Collapsed"`) |
| `FLinearColor` / `FSlateColor` | `[r,g,b,a]` or `{ "SpecifiedColor": [r,g,b,a] }` |
| struct (`FMargin`, `FAnchors`, `FVector2D`, `FSlateChildSize`) | nested JSON object/array |
| `FSlateBrush` | nested object; `ResourceObject` = asset path string (texture/material) |
| object/asset ref | asset path string, resolved via `LoadObject` |

Risk: `FSlateBrush` and `FText` are the fiddliest; unresolved asset refs / unknown props → recorded as
`warning` + `manual_check_required` (never silently dropped). This mirrors the existing "unknown → keep + warn"
principle.

---

## 7. Custom UserWidget support

- In `hierarchy.type`, a custom widget is given by its **generated class path**
  (`/Game/UI/WBP_MyButton.WBP_MyButton_C`) or a resolvable class; the agent `LoadObject<UClass>`s it and
  `ConstructWidget`s it into the tree.
- If the class can't be loaded (not compiled / bad path) → `failed` for that node + `manual_check_required`.
- Dependencies on custom widgets are recorded in the Widget IR (`class` path) and in the manifest's
  dependency list, so callers see what the WBP needs.
- Exposed properties on the custom widget (its `UPROPERTY(BindWidget)` / `meta=(ExposeOnSpawn)` etc.) are set
  through the same reflection path (§6).

---

## 8. Atomic capability status table

| Atomic capability | Now | Key UE APIs | Risks | Validation | Version-sensitive |
|---|---|---|---|---|---|
| create_widget_blueprint_asset | ❌ | `UWidgetBlueprintFactory` / `FKismetEditorUtilities::CreateBlueprint` (UMGEditor) | factory editor-only; correct generated-class | asset exists + opens in editor | Med (UMGEditor) |
| create_widget_tree_root | ❌ | `WBP->WidgetTree->ConstructWidget`, set `RootWidget` | outer/ownership | redump root present | Low |
| add_widget | ❌ | `UPanelWidget::AddChild` → `UPanelSlot` | legal child/slot for parent | redump child under parent | Low |
| remove_widget | ❌ | `UPanelWidget::RemoveChild` / `Widget->RemoveFromParent` | orphan refs / graph refs | redump absent; compile OK | Low |
| move_widget | ❌ | remove + AddChild to new parent; preserve slot where possible | slot type change | redump new parent | Low |
| rename_widget | ❌ | `FBlueprintEditorUtils::RenameMemberVariable`-style / `Widget->Rename` + var rename | break graph refs to old name | compile OK; graph refs updated | Med |
| set_widget_property | ❌ | `FProperty` + `FJsonObjectConverter`/`ImportText_Direct` | brush/text/struct import | redump prop equals input | Low-Med |
| set_slot_property | ❌ | slot `FProperty` setters / reflection | slot class must match parent | redump slot prop equals input | Low-Med |
| add_custom_user_widget | ❌ | `LoadObject<UClass>("....._C")` + ConstructWidget | class resolution / circular deps | redump class path; compile OK | Low |
| bind_widget_event | ❌ | `UK2Node_ComponentBoundEvent` (ComponentProperty=widget, Delegate=OnClicked) | fiddly node setup; delegate discovery | event node present + wired; compile OK | **High** |
| bind_widget_property | ❌ | `WBP->Bindings` (`FDelegateEditorBinding`) | getter signature match | binding present; runtime value | Med |
| create_widget_variable | ⚠️ | `FBPGen::AddVariable` (reuse) | none new | redump variable | Low |
| create_function | ⚠️ | `FBPGen::AddFunctionGraph` (signature); **body wiring TBD** | bodies not auto-wired today | redump function | Low |
| create_event_graph_logic | ⚠️ | `FBPGen` spawn/connect (reuse) | limited node set today | redump nodes/edges | Low |
| create_umg_animation | ❌ | `UWidgetAnimation` + MovieScene tracks/sections | very complex API | redump animation; open in editor | **High** |
| compile_blueprint | ✅ | `FKismetEditorUtilities::CompileBlueprint` | none | compile log | Low |
| save_blueprint | ✅ | `UPackage::SavePackage` | none | file mtime | Low |
| redump_ir (widget) | ❌ | `FBPGenIRDumper` + WidgetTree traversal (new) | struct serialization | valid JSON; nodes present | Low |
| generate_preview (hierarchy) | ⚠️ | DOT/Mermaid of widget_tree → PNG (Graphviz) | none (graph-only) | png exists | Low |
| generate_preview (rendered UMG) | ❌ | `FWidgetRenderer::DrawWidget` + off-screen RHI | **no render under -nullrhi**; needs RHI | image matches editor | **High / uncertain** |
| compare_expected_vs_actual | ⚠️ | reuse `compare_ir` over Widget IR | matching heuristics | diff report | Low |

---

## 9. Phased acceptance plan

- **Phase 1 — Minimal WBP**: empty `UserWidget` + CanvasPanel root + TextBlock + Button; compile+save+open.
  Gate: asset opens; redump shows 3 widgets; no compile errors.
- **Phase 2 — Hierarchy ops**: add/remove/move/rename + slot props (Canvas/HBox/VBox/Overlay). Gate: redump
  hierarchy matches spec; slots correct; compile OK.
- **Phase 3 — Details props**: Text/Color/Brush/Visibility/Padding/Anchors/Alignment/Size/RenderTransform.
  Gate: redump values equal input **and** match what the UE editor shows (manual spot-check the fiddly ones:
  brush/text/color).
- **Phase 4 — Events & graph**: Button `OnClicked` → Custom Event/Function with graph nodes/edges. Gate: bound
  event node present + wired; compile OK.
- **Phase 5 — Custom widgets**: add a project `UserWidget`; resolve class, set props, dependency in IR + preview.
  Gate: class resolved; redump shows custom class; compile OK.
- **Phase 6 — Full from-zero**: one complete JSON spec → hierarchy + details + variables + events + graph +
  preview + result report. (UMG **Animation** rides here but is the highest risk — may ship as
  `manual_check_required` initially.)

Each phase produces `manifest.json` + `created_ir.json`(widget) + hierarchy preview + `compare` (Phase ≥2).

---

## 10. New / changed scripts, commandlets, modules, docs

- **Plugin**: `Build.cs` (+`UMG`,`UMGEditor`); new `FBPWidgetGen` (.h/.cpp); extend `FBPGenIRDumper` (Widget IR);
  extend `FBPCreate::Run` (widget branch) and `FBPATEdit` (widget ops). Optional later: `MovieScene` for anim.
- **Commandlets**: reuse `BPCreate` / `BPATEdit` (no new entry points).
- **Scripts**: `create_blueprint.ps1` / `edit_blueprint.ps1` add a widget-hierarchy preview render step; no new
  script strictly required (dispatcher already routes `create`/`edit`).
- **Docs**: extend `agent_create_contract.md` (widget), new `widget_blueprint_schema.md` (this design frozen),
  update `request_schemas.md` + `blueprint_ir_schema.md` (widget_tree), add widget risks to `issue_patterns.md`.

---

## 11. How other AIs call it

Unchanged entry — one `request.json` through `blueprint_agent.ps1`:

```powershell
.\scripts\blueprint_agent.ps1 -RequestJson ".\create_wbp.json" -Mode auto -PreferEditorLive
```

`create` with `asset.blueprint_type:"Widget"` + `widget.hierarchy` builds the WBP; the AI reads
`manifest.json` (status/outputs), `created_ir.json` (widget_tree), and the hierarchy preview. Widget edits go
through the `edit` task with widget operations (`add_widget`, `set_widget_property`, `bind_widget_event`, …).
Same safety model: `overwrite_policy`, backup/rollback on edit, compile/save gated, assets never touched
outside an explicit create/edit.

---

## 12. Can we enter implementation?

**Yes — incrementally, not all at once.**

- **Green (implement now, moderate effort, low/med risk)**: Phases 1–3 + hierarchy preview + Widget IR. This
  delivers real value (create WBP with hierarchy, slots, common Details; verify by redump).
- **Yellow (implement after green, needs care)**: Phase 4 event binding (`UK2Node_ComponentBoundEvent` is
  fiddly), Phase 5 custom widgets (class resolution), function-body wiring, property bindings.
- **Red (defer / partial / manual)**: UMG **Animation** (MovieScene complexity) and **pixel-accurate UMG
  render** (headless RHI uncertainty). Ship these as `manual_check_required` until proven; the hierarchy
  diagram covers "preview" in the meantime.

**Hard dependencies before Phase 1**: add `UMG`/`UMGEditor` modules and confirm `UWidgetBlueprintFactory` +
`ConstructWidget` work in a **commandlet** context on the target engine (incl. custom engines) — this is the
first thing to validate, and is moderately version-sensitive.

**Recommendation**: proceed with a Phase-1 spike (create empty `UserWidget` + Canvas + TextBlock + Button,
compile/save, and dump a Widget IR) to de-risk the UMGEditor-in-commandlet assumption before building the
full `FBPWidgetGen`. Everything else is well-understood and reuses the existing create/edit/dump/compare
pipeline.
