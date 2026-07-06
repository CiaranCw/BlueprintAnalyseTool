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
Editor must be **closed** (creation writes/saves). Exit codes: `0 success, 10 partial (compile
warnings), 20 failed, 30 bad input, 41 exists_refused`.

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
  manifest/create_result, and the redumped nodes + per-widget `bindable_events` appear in `created_ir.json`. Deferred
  (warn/manual): handler exec-wiring to a named custom_event/function, property binding (`widget.bindings`), custom
  `UserWidget` children insertion, UMG animation, pixel-accurate render. See `docs/widget_blueprint_schema.md`.
- AnimBlueprint creation → clear `failed` (needs its factory); use Actor/Component/Interface/Widget.
- Unsupported `node_type` → warning + `manual_check_required`, never silently dropped.
- After creation, open the asset in the editor to visually confirm.

## Verified
Creating `/Game/Generated/BP_AgentCreatedExample` (Actor, parent Actor) with `Health:int`, `SceneRoot`
SceneComponent, `OnHealthChanged` dispatcher, and an EventGraph `BeginPlay → PrintString` edge →
status=success, real 37 KB `.uasset` written, compiled & saved, 0 warnings.
