# Agent Create Contract — spec-driven Blueprint creation

Create a new Blueprint asset from a structured JSON spec. Write task (creates + compiles + saves the
new asset); never touches unrelated assets; honours `overwrite_policy`.

## Entry points
```powershell
# via the unified agent (task_type=create):
.\scripts\blueprint_agent.ps1 -RequestJson ".\create_request.json"

# or directly:
.\scripts\create_blueprint.ps1 -ProjectUProject "<...>.uproject" -SpecFile ".\create_spec.json" [-UERoot "<engine>"] [-OutputDir "<dir>"]

# or commandlet:
UnrealEditor-Cmd.exe "<...>.uproject" -run=BPCreate -SpecFile="<spec>.json" -OutputDir="<dir>" -unattended -nop4
```
Editor must be **closed** for the commandlet path (creation writes/saves). For the **`editor_live` path**
(editor OPEN), submit through the in-editor service (`docs/editor_live_mode.md`): it runs the same builders
but adds a property preflight, a request journal, idempotency, and refuses during PIE
(`blocked_by_editor_state`). Exit codes: `0 success, 10 partial|success_with_warnings (compile/preflight
warnings), 20 failed (incl. preflight blocked a required property), 30 bad input, 41 exists_refused`.

### Preflight (editor_live create; `execution.run_preflight`, default true)
Before building, `FBPPreflight` resolves every widget/property in the spec against the resolved classes and
writes `preflight_report.json` / `normalized_request.json` / `capability_snapshot.json`. A **required**
property miss blocks the create (`failed`, exit 20); an **optional** miss (`optional_properties` /
`property_semantics` on the hierarchy node) is a warning (`success_with_warnings`, exit 10). Match kinds:
`exact_match | alias_match | display_name_match | property_absent | property_read_only |
property_type_mismatch | ambiguous_match`. As always, **analyze first** for pure-`UserWidget` rows that
expose no C++ property surface (see `docs/issue_patterns.md` P18).

## Spec (request.request)
See the `create` section of `docs/request_schemas.md`. Summary:
- `asset{ asset_path, blueprint_type(Actor|ActorComponent|Interface|Widget), parent_class, overwrite_policy }`.
- `variables[]`, `components[]`, `functions[]` (signatures), `event_dispatchers[]`,
  `graphs[]{ nodes[], edges[], comments[] }`.
- `blueprint_type="Widget"`: add `widget.hierarchy` (recursive WidgetTree) — see `docs/widget_blueprint_schema.md`.
- Node authoring supported for the EventGraph: `event, call_function, branch, sequence, variable_get,
  variable_set, comment`. Edges use `local_id.PinName`.

## overwrite_policy
- `fail_if_exists` (default): if the package exists → status `failed` / exit 41, nothing written.
- `create_unique_name`: append `_1/_2/...` until free; records the chosen name in warnings.
- `overwrite_if_allowed`: proceed over the existing asset (use with care).

## Outputs (under OutputDir)
```
manifest.json        # status/task_type=create/mode=native_full/created_asset/counts/warnings/errors
create_result.json   # status + overwrite_policy + warnings/errors
created_ir.json       # full IR of the created blueprint (FBPGenIRDumper)
summary.md
viz/created.dot
```

## Guarantees / limits
- Reuses the proven `FBPGen` builders (same code that generates the test suite) → creates real,
  compilable assets.
- Function `body` graphs and component `attach_to` hierarchy are recorded as `manual_check_required`
  (signatures/flat components are created; deep body wiring is not auto-generated in this version).
- **Widget Blueprint (UMG)**: supported (Phase 1-4) — WidgetTree hierarchy, slots, and Details via reflection;
  `created_ir.json` gains `widget_tree` and `viz/hierarchy.dot|.mmd` is produced. **Widget events** (`widget.events`)
  are bound **generically by reflection** (any BlueprintAssignable multicast delegate on any widget — Button/CheckBox/
  ComboBox/Slider/EditableTextBox/SpinBox/ScrollBox/custom UserWidget), creating a `UK2Node_ComponentBoundEvent` per
  event; results (incl. parameters, idempotent `reused`, and classified failures) go to `widget_event_bindings` in
  manifest/create_result, and the redumped nodes + per-widget `bindable_events` appear in `created_ir.json`. **Custom `UserWidget` children**:
reference a project widget by class/asset path in `hierarchy.type` (`/Game/..._C`, object, or package form — all
normalized to the `_C` generated class); it is constructed as a black box with slot/Details/events + recorded under
`dependencies` (`type=custom_user_widget`). See `docs/widget_blueprint_schema.md`.
- **Widget event handler wiring (Phase 4 P2)**: `events[].handler.type` = `bound_event` (default; the bound-event
  node is the entry) | `custom_event` (ensure a Custom Event `name`, route bound event → it) | `function` (ensure a
  function `name`, wire a call node). Flags: `create_if_missing`/`connect_exec`/`connect_parameters` (all default
  true). Exec is connected first, then data params matched by name→type; results (per-param connected/mismatch/
  ambiguous/missing + handler-level `handler_not_found|function_is_pure|exec_*`) are recorded under
  `widget_event_bindings[].handler` and redumped in `created_ir.json`. Idempotent (bound event, custom event/
  function, and call node all reused). An optional `handler.body` generates MVP logic inside the handler
  (`print_string` / `set_text`, literal or `from_param`; recorded as `handler.body_ops[]`). Deferred (warn/manual):
  richer handler body logic, property binding (`widget.bindings`), custom-widget internal expansion, UMG animation,
  pixel-accurate render.
- **Full-spec create**: a single request can build a complete interactive WBP (root + title + custom Setting Items +
  native widgets + slots + Details via `settable_properties` names + events + handlers + bodies). Outputs add
  `viz/graph.dot|.mmd` (event-graph preview) alongside `viz/hierarchy.dot|.mmd`; `scripts/compare_widget_spec.ps1`
  emits an expected-vs-actual `compare_report.json` (verified UE 5.4 AClient: `WBP_Agent_FullSpec_SettingsPanel`,
  widgets 8/8 + events 4/4 match).
- **Post-write shutdown crash**: `scripts/create_blueprint.ps1` never fakes failure — if the commandlet crashes in
  engine teardown *after* writing complete artifacts with `status=success`, it reports `success_with_exit_warning`
  (exit 0), records the raw exit code + `logs/create_stdout.txt` and a `post_exit` block + warning in the manifest.
- **CanvasPanelSlot anchor-aware geometry**: setting a slot `Position`/`Size` on a **stretched** axis
  (`Anchors.Minimum != Maximum`, where `Offsets` are margins) is guarded — it is skipped with a
  `canvas_slot_stretch_axis_size_warning` unless `slot.allow_stretch_axis_size_override:true`. Use `Offsets`/
  `LayoutData` for stretch axes; anchor-defining keys are applied first so the guard sees the final anchors. Slot IR
  carries `slot.geometry_semantics`. See `docs/widget_blueprint_schema.md`.
- **Widget property names**: keys in a widget/slot `properties` object are resolved with **alias matching**
  (exact → case-insensitive → bool `b` prefix → Details DisplayName → strip space/underscore), then a `Set<Key>`
  setter fallback. An aliased write is reported as `property_alias_matched` and a miss as `property_not_found`
  **with `suggestions`** in `manifest`/`create_result` `property_notes[]` (never silent). To avoid guessing, first
  **analyze** the target/custom widget and read each widget's `settable_properties` (use the internal `name`, not
  the display name; bool props often carry a `b` prefix, e.g. `Default Checked` → `bDefaultChecked`). See
  `docs/blueprint_ir_schema.md` §10.1 and `docs/issue_patterns.md` P16.
- AnimBlueprint creation → clear `failed` (needs its factory); use Actor/Component/Interface/Widget.
- Unsupported `node_type` → warning + `manual_check_required`, never silently dropped.
- After creation, open the asset in the editor to visually confirm.

## Verified
Creating `/Game/Generated/BP_AgentCreatedExample` (Actor, parent Actor) with `Health:int`, `SceneRoot`
SceneComponent, `OnHealthChanged` dispatcher, and an EventGraph `BeginPlay → PrintString` edge →
status=success, real 37 KB `.uasset` written, compiled & saved, 0 warnings.
