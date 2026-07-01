# Request Schemas — one JSON to call the Blueprint Agent

Other AIs call the agent with a single `request.json` via `scripts/blueprint_agent.ps1`:

```powershell
.\scripts\blueprint_agent.ps1 -RequestJson ".\request.json" [-OutputDir "<override>"] [-UERoot "<override>"] [-ProjectUProject "<override>"]
```

## Envelope (all task types)
> First-time on a new project? Call `task_type=status` (read-only, no UE) to learn the stage and whether
> a one-time `warmup` is needed, then proceed. See `docs/warmup_and_capability_state.md`.

```json
{
  "schema_version": "1.0",
  "task_type": "status|warmup|analyze|edit|create|validate",
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
    "mode": "auto|native_full|python_partial|offline_asset_scan",
    "strict": false, "read_only": true, "create_backup": true,
    "allow_destructive_edit": false, "render_preview": true
  },
  "request": { }
}
```
- `project.ue_root` may be omitted → auto-resolved from `.uproject` EngineAssociation (version → launcher; GUID → source/custom build registry).
- `engine_policy.allow_project_plugin_install`/`allow_incremental_compile` gate native_full's invasive steps (analyze/create need native for full graph).
- Dispatcher writes `<output_dir>/<task_type>/dispatch_manifest.json`; the specialized tool writes the detailed `manifest.json`/`edit_result.json` under `sub_output_dir`.

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
remove_node(+preserve_exec), add_reroute_on_edge, add_variable, set_variable_default`. Node selectors:
`node_class / node_title / node_title_contains / function_name / node_id / match_index /
exec_out_connected / exec_in_connected`. Destructive ops need `allow_destructive_edit` (apply modes);
`plan-only`/`dry-run` always preview safely. Outputs: `edit_plan.json / edit_result.json / diff_report.json /
baseline_ir.json / modified_ir.json / summary.md / viz/*`. See `docs/agent_edit_contract.md`.

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
warning + `manual_check_required` (never silently dropped). `blueprint_type` Actor/ActorComponent/Interface
supported; WidgetBlueprint/AnimBlueprint creation is not yet supported (clear failure). Outputs:
`create_result.json, created_ir.json, summary.md, viz/created.dot, manifest.json`. See `docs/agent_create_contract.md`.

## task_type = validate  →  scripts/validate_outputs.ps1
Static validation of prior deliverables (JSON well-formed, edge referential integrity, viz presence).

## Data structures
Unified IR / Node / Pin / Edge shapes are in `docs/blueprint_ir_schema.md`. Unknown nodes keep their
real `node_class`/title/pins (`node_type="unknown"`), never dropped. Edge types:
`exec|data|delegate|object_ref|interface_target|cast_object|latent|reroute|unknown`.

## Status values (dispatch_manifest.json / manifest.json)
`success | partial | failed | rolled_back | exists_refused | bad_input`. An AI reads the manifest to
decide next steps; `partial` (mode != native_full) means graph structure is incomplete — re-run
native_full with the engine_policy flags (after user consent) for the full IR.
