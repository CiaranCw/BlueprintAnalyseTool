# Request Schemas — one JSON to call the Blueprint Agent

Other AIs call the agent with a single `request.json` via `scripts/blueprint_agent.ps1`:

```powershell
.\scripts\blueprint_agent.ps1 -RequestJson ".\request.json" [-OutputDir "<override>"] [-UERoot "<override>"] [-ProjectUProject "<override>"]

# or without a file (handy for status / quick analyze):
.\scripts\blueprint_agent.ps1 -Task status  -Mode editor_live -ProjectUProject "<...>.uproject" [-TimeoutSeconds 20]
.\scripts\blueprint_agent.ps1 -Task analyze -Mode auto -PreferEditorLive -ProjectUProject "<...>.uproject" -AssetPaths "/Game/UI/WBP_X" [-TimeoutSeconds 60]
```

`-Mode` overrides `execution.mode`; `-PreferEditorLive` prefers an already-open editor; `-TimeoutSeconds`
bounds the editor_live wait (never hangs). See `docs/editor_live_mode.md` for the full editor_live flow.

## Envelope (all task types)
> First-time on a new project? Call `task_type=status` (read-only, no UE) to learn the stage and whether
> a one-time `warmup` is needed, then proceed. See `docs/warmup_and_capability_state.md`.

```json
{
  "schema_version": "1.0",
  "task_type": "status|warmup|analyze|edit|create|validate|update",
  "project": {
    "ue_root": "D:/AEngine",
    "uproject": "D:/AClient/AClient.uproject",
    "output_dir": "D:/AClient/Saved/BPParserAgentReports",
    "engine_policy": {
      "allow_project_plugin_install": true,
      "allow_incremental_compile": true,
      "allow_python_fallback": true,
      "allow_offline_fallback": true
    }
  },
  "execution": {
    "mode": "auto|editor_live|native_full|python_partial|offline_asset_scan",
    "strict": false, "read_only": true, "create_backup": true,
    "allow_destructive_edit": false, "render_preview": true
  },
  "request": { }
}
```
- `project.ue_root` may be omitted → auto-resolved from `.uproject` EngineAssociation (version → launcher; GUID → source/custom build registry).
- `engine_policy.allow_project_plugin_install`/`allow_incremental_compile` gate native_full's invasive steps (analyze/create need native for full graph).
- `execution.mode=auto` probes **editor_live first** (reuse an open editor), then native_full → python_partial → offline. `editor_live` (explicit) never launches UnrealEditor-Cmd; if no service answers it returns `editor_live_unavailable` (exit 24). See `docs/editor_live_mode.md`.
- Dispatcher writes `<output_dir>/<task_type>/dispatch_manifest.json` (now includes an `editor_live{attempted,available,fallback_from,fallback_to}` block); the specialized tool writes the detailed `manifest.json`/`edit_result.json` under `sub_output_dir`.

## task_type = status  →  scripts/agent_status.ps1  (READ-ONLY, no UE launch, always safe first call)
```json
"request": {}
```
Emits `capability_state.json`: `stage` (offline_only|python_only|needs_install|needs_build|native_ready),
`available_modes`, `capabilities{understand_full,edit,create}`, `warmup_required`, `recommended_action`,
`next_calls`. Use it to decide whether to warmup or go straight to work.

## task_type = warmup  →  scripts/warmup_project.ps1  (one-time; needs consent)
Requires `project.engine_policy.allow_incremental_compile=true`. Installs the read-only source plugin +
incrementally builds the project's Editor target, enabling native_full. Optional:
```json
"request": { "smoke_asset_path": "/Game/Some/Blueprint" }
```
Emits `warmup_state.json` (`native_full_ready`). Never modifies blueprint assets. See `docs/warmup_and_capability_state.md`.

## task_type = analyze  →  scripts/analyze_blueprint.ps1
```json
"request": {
  "asset_paths": ["/Game/UI/WBP_MainMenu"],
  "include": { "graphs":true,"nodes":true,"pins":true,"edges":true,"variables":true,"functions":true,
               "macros":true,"event_dispatchers":true,"components":true,"interfaces":true,"dependencies":true,
               "timelines":true,"comments":true,"metadata":true },
  "visualization": { "generate_dot":true,"generate_mermaid":true,"generate_png":true,"generate_svg":true,
                     "split_large_graphs":true,"max_nodes_per_graph_image":80 }
}
```
Outputs (per asset): `manifest.json`, `blueprint_ir.json`|`partial_ir.json`, `summary.md`,
`understanding_score.json`, `graphs/*.json`, `viz/blueprint.dot|.mmd`, `logs/*`.

## task_type = edit  →  scripts/edit_blueprint.ps1 (BPATEdit)
```json
"request": {
  "asset_path": "/Game/Blueprints/BP_PlayerCharacter",
  "intent": "Insert a Branch after BeginPlay",
  "mode": "plan-only|dry-run|apply|apply-and-verify",
  "allow_destructive_edit": false, "create_backup": true,
  "operations": [
    { "op_id":"op_001", "operation":"insert_node_between", "graph":"EventGraph",
      "from_node":{ "node_class":"K2Node_Event", "node_title_contains":"BeginPlay", "exec_out_connected":true },
      "to_node":{ "node_class":"K2Node_CallFunction", "function_name":"PrintString" },
      "new_node":{ "node_class":"K2Node_IfThenElse" } }
  ]
}
```
Operations: `set_pin_default_value, connect_pins, disconnect_pins, add_node, insert_node_between,
remove_node(+preserve_exec), add_reroute_on_edge, add_variable, set_variable_default, set_parent_class`.
Widget-Blueprint tree ops (edit an existing WBP; see `agent_edit_contract.md` §3c). Note: `set_slot_property`
`Position`/`Size` on a **stretch** CanvasPanelSlot axis is guarded (skipped + `canvas_slot_stretch_axis_size_warning`);
use `property:"Offsets" {Left,Top,Right,Bottom}` / `"LayoutData"` for stretch axes, or
`allow_stretch_axis_size_override:true` to force. Analyze `slot.geometry_semantics` first.
`set_widget_property{widget,property,value}, set_slot_property{widget,property,value},
add_widget{parent,widget{name,type,properties?,slot?}}, bind_widget_event{widget,event,handler?},
remove_widget{widget} (destructive), move_widget{widget,new_parent} (destructive)`. The edit `diff_report.json`
then also carries `added/removed/moved_widgets, modified_widget_properties, modified_slot_properties,
added_event_bindings, modified_event_handlers`, and WBP edits emit `viz/hierarchy.before|after.dot`. Node selectors:
`node_class / node_title / node_title_contains / function_name / node_id / match_index /
exec_out_connected / exec_in_connected`. Destructive ops need `allow_destructive_edit` (apply modes);
`plan-only`/`dry-run` always preview safely. Outputs: `edit_plan.json / edit_result.json / diff_report.json /
baseline_ir.json / modified_ir.json / summary.md / viz/*`. See `docs/agent_edit_contract.md`.

**Reparent** (change an existing Blueprint's parent — distinct from create-time `asset.parent_class`):
```json
{ "op_id":"rp1", "operation":"set_parent_class",
  "new_parent_class":"/Script/AClient.RGUserWidget",
  "options":{ "create_backup":true, "compile":true, "save":true, "rollback_on_failure":true } }
```
Alias `reparent_blueprint`. `new_parent_class`: C++ `/Script/Module.Class` or Blueprint `/Game/.../BP.BP_C` (also
`.BP` / package form). Family-checked (WBP→Actor etc. fail with `incompatible_parent_type`); Interface/Macro/Function
Library/AnimBlueprint rejected; compile-failure restores the old parent (rolled_back, asset unchanged).
`edit_result.json` adds `old_parent_class/new_parent_class/new_parent_source/compile_status/rollback_performed`;
`diff_report.json` adds `modified_asset.parent_class{before,after}`. See `docs/agent_edit_contract.md` §3b.

## task_type = create  →  scripts/create_blueprint.ps1 (BPCreate)
```json
"request": {
  "asset": { "asset_path":"/Game/Generated/BP_X", "blueprint_type":"Actor|ActorComponent|Interface",
             "parent_class":"/Script/Engine.Actor", "overwrite_policy":"fail_if_exists|create_unique_name|overwrite_if_allowed" },
  "components": [ { "name":"SceneRoot", "class":"/Script/Engine.SceneComponent", "attach_to":null } ],
  "variables": [ { "name":"Health", "type":{ "category":"int", "container_type":"none" }, "default_value":"100", "editable":true } ],
  "functions": [ { "name":"ComputeRatio", "pure":true, "inputs":[{ "name":"Max","type":{"category":"float"} }], "outputs":[{ "name":"Ratio","type":{"category":"float"} }] } ],
  "event_dispatchers": [ { "name":"OnHealthChanged", "parameters":[{ "name":"NewHealth","type":{"category":"int"} }] } ],
  "graphs": [ { "graph_name":"EventGraph", "graph_type":"event",
    "nodes":[ { "local_id":"begin","node_type":"event","event_name":"ReceiveBeginPlay","position":{"x":0,"y":0} },
              { "local_id":"print","node_type":"call_function","function":"/Script/Engine.KismetSystemLibrary.PrintString","position":{"x":350,"y":0},"defaults":{"InString":"hi"} } ],
    "edges":[ { "from":"begin.then", "to":"print.execute", "edge_type":"exec" } ],
    "comments":[ { "text":"created", "bounds":{"x":-100,"y":-100,"w":800,"h":300} } ] } ]
}
```
Supported `node_type`: `event, call_function (function="/Script/Pkg.Class.Func"), branch, sequence,
variable_get, variable_set, comment`. Edges reference `local_id.PinName`. Unsupported node types →
warning + `manual_check_required` (never silently dropped). `blueprint_type` Actor/ActorComponent/Interface/**Widget**
supported (AnimBlueprint creation not yet). Outputs:
`create_result.json, created_ir.json, summary.md, viz/created.dot, manifest.json`. See `docs/agent_create_contract.md`.

For `blueprint_type:"Widget"` add a `widget.hierarchy` (recursive WidgetTree: `type`/`name`/`properties`/`slot`/
`children`); Details + slot props are set by reflection (+ `Set<Key>` setter fallback for CanvasPanel Position/
Size/Anchors/Alignment). Output adds `created_ir.json.widget_tree` + `viz/hierarchy.dot|.mmd`.

**Widget property names — analyze first, don't guess.** A `properties` key is resolved by alias matching
(exact → case-insensitive → bool `b` prefix → Details DisplayName → strip space/underscore) then a `Set<Key>`
setter; an alias write emits `property_alias_matched` and a miss emits `property_not_found` **with `suggestions`**
in `manifest`/`create_result` `property_notes[]`. Every widget in the IR (analyze/redump) carries
`settable_properties` + `slot_settable_properties` — `{name, display_name, type, declaring_class, editable,
blueprint_visible, blueprint_read_only, deprecated, current_value, set_supported, notes}`. Use the internal
`name` (bool props often carry a `b` prefix, e.g. `Default Checked` → `bDefaultChecked`). See
`docs/blueprint_ir_schema.md` §10.1 and `docs/issue_patterns.md` P16.

`widget.events` binds widget delegates **generically by reflection** (any BlueprintAssignable multicast delegate)
and wires the bound-event exec/params to a handler (Phase 4 P2):
```json
"events": [
  { "widget": "PlayButton",   "event": "OnClicked",          "handler": { "type": "custom_event", "name": "OnPlayClicked",       "create_if_missing": true, "connect_exec": true, "connect_parameters": true } },
  { "widget": "QualityCombo", "event": "OnSelectionChanged", "handler": { "type": "function",     "name": "HandleQualityChanged" } }
]
```
`handler.type`: `bound_event` (default; bound-event node is the entry) | `custom_event` (create/reuse a Custom Event
`name`, route the bound event into it) | `function` (create/reuse a function `name`, wire a call node; pure →
`function_is_pure`). Flags `create_if_missing`/`connect_exec`/`connect_parameters` default true; `create_if_missing=false`
+ missing handler → `handler_not_found`. Data params are matched by name→type. An optional `handler.body` array
generates simple logic inside the handler (MVP: `{op:"print_string", text|from_param}`, `{op:"set_text", target,
text|from_param}`) — recorded as `handler.body_ops[]`. Example:
```json
{ "widget":"ApplyButton", "event":"OnClicked", "handler": { "type":"custom_event", "name":"OnApplyClicked",
  "body":[ {"op":"print_string","text":"Applied"}, {"op":"set_text","target":"TitleText","from_param":"SelectedItem"} ] } }
```
Create writes a widget hierarchy preview (`viz/hierarchy.dot|.mmd`) AND an event-graph preview
(`viz/graph.dot|.mmd`, nodes+edges, exec solid / data dashed) plus a `compare_report.json` when run via
`scripts/compare_widget_spec.ps1` (expected-vs-actual widgets + events). Each result → `widget_event_bindings`
(manifest/create_result/created_ir) with bind `status`
(`bound|reused|widget_not_found|not_variable|property_missing|delegate_not_found|pins_incomplete|error`), `parameters`,
and a nested `handler` object (`connected/exec_connected/parameters_connected[{from,to,status}]` + handler failure
codes). Idempotent (bound event, handler entry, and call node all reused). Analyze may set
`include.widget_bindable_events` / `include.widget_settable_properties` (default true) to control the per-widget
discovery arrays for large WBPs (every widget in the IR carries `bindable_events` and `settable_properties`).

**Custom UserWidget children**: set a hierarchy node's `type` to a project widget's class/asset path
(`/Game/UI/WBP_X.WBP_X_C`, `/Game/UI/WBP_X.WBP_X`, or `/Game/UI/WBP_X` — all normalized to `_C`). It is inserted
as a black box (slot/Details/events supported) and recorded under `dependencies`
(`{type:custom_user_widget, asset_path, generated_class}`). Failure categories: `class_path_invalid|
class_load_failed|not_user_widget|construct_widget_failed|parent_not_panel|property_set_failed`.

Deferred (warn/manual): handler exec-wiring to custom_event/function, property binding, custom-widget internal
expansion, UMG animation, pixel render. Full schema: `docs/widget_blueprint_schema.md`.

## task_type = validate  →  scripts/validate_outputs.ps1
Static validation of prior deliverables (JSON well-formed, edge referential integrity, viz presence).

## task_type = update  →  scripts/update_agent_in_project.ps1
Keep an INSTALLED agent current from a source agent repo (idempotent, backed up, non-destructive).
```json
"request": { "source_agent_root": "D:/Projects/BlueprintAgent", "mode": "copy|reference",
             "dry_run": false, "run_warmup_after_update": false, "allow_uproject_edit": false }
```
Refreshes only managed content (`Tools/BlueprintAgent/{scripts,docs,plugin}`, `.cursor`/`.claude`,
`*.template.json`, and the `AGENTS.md`/`CLAUDE.md` managed block); preserves user files/prose; reports
conflicts (`modified_in_target`, backed up before replace); marks `needs_warmup_after_update` when the
plugin source changed; never modifies blueprint assets or `.uproject` (unless `allow_uproject_edit`). Outputs
`update_plan.json` + `update_result.json` under `Saved/BPParserAgentReports/update/<ts>/`. First probe with
`scripts/check_project_agent_version.ps1` (→ `check_result.json`). Full protocol: `docs/update_sync_protocol.md`.

## editor_live (in-editor, file-queue) — reuse an already-open UE editor
When the target project's UE editor is open with the plugin loaded, `BPAgentLiveService` polls a request
queue, so no new UnrealEditor-Cmd is launched. Two ways in:

- **Via the dispatcher (recommended):** `blueprint_agent.ps1 -Mode editor_live` (or `auto -PreferEditorLive`).
  It translates the envelope above into a queue request through `scripts/editor_live_client.ps1`, waits
  (bounded by `-TimeoutSeconds`), rasterizes PNG/SVG when Graphviz is present, and records fallback.
- **Directly (any language):** write the payload + a `.ready` commit marker, then poll `outbox`:

```text
inbox : <Project>/Saved/BPParserAgentRequests/inbox/<id>.request.json   (payload)
        <Project>/Saved/BPParserAgentRequests/inbox/<id>.ready          (commit marker; service ignores half-written requests)
report: <output_dir or Saved/BPParserAgentReports>/editor_live/<id>/manifest.json (+ blueprint_ir.json/summary.md/viz/logs)
outbox: <Project>/Saved/BPParserAgentRequests/outbox/<id>.done | <id>.failed  (JSON: {request_id,exit_code,status,manifest})
```

Direct request payload (consumed by the plugin):
```json
{
  "schema_version": "1.0", "request_id": "req_20260704_001",
  "task_type": "status|analyze|edit|create", "mode": "editor_live",
  "asset_paths": ["/Game/UI/WBP_MainMenu"],
  "asset_path": "/Game/Blueprints/BP_X",
  "execution": {
    "read_only": true, "strict": false, "render_preview": true,
    "use_loaded_editor_state": true, "allow_dirty_assets": false,
    "allow_edit": false, "create_backup": true, "allow_destructive_edit": false,
    "allow_create": false, "require_user_ack": false, "allow_edit_during_pie": false
  },
  "edit":   { "...": "same shape as the edit request.operations" },
  "create": { "...": "same shape as the create request.asset/variables/graphs" },
  "output_dir": "D:/AClient/Saved/BPParserAgentReports"
}
```

status output (`editor_live/<id>/manifest.json`) reports `editor_live.available/service_running/supports`
and `current_editor_state{is_pie,is_saving,is_compiling_blueprints,dirty_assets_count}`. analyze/edit/create
manifests add an `editor_live` block with `source_state`
(`loaded_clean_memory|loaded_dirty_memory|disk_saved_asset|unknown`) and the `editor_state` snapshot.

Safety: analyze is read-only (no save/compile/mutation). `edit` needs `read_only=false`+`allow_edit=true`
and is refused during PIE (unless `allow_edit_during_pie`) or on a dirty target open in the asset editor
(unless `require_user_ack`). `create` needs `allow_create=true` and is refused during PIE. During
save/compile the service waits (bounded) then fails rather than hanging.

## Data structures
Unified IR / Node / Pin / Edge shapes are in `docs/blueprint_ir_schema.md`. Unknown nodes keep their
real `node_class`/title/pins (`node_type="unknown"`), never dropped. Edge types:
`exec|data|delegate|object_ref|interface_target|cast_object|latent|reroute|unknown`.

## Status values (dispatch_manifest.json / manifest.json)
`success | partial | failed | rolled_back | exists_refused | bad_input`. An AI reads the manifest to
decide next steps; `partial` (mode != native_full) means graph structure is incomplete — re-run
native_full with the engine_policy flags (after user consent) for the full IR.
