# Agent Edit Contract — Atomic Blueprint Editing (BPATEdit)

This document is the callable contract other agents (Claude Code CLI, Cursor Agent, CI) use to
apply **atomic, plan-based, verifiable, reversible** edits to an existing Blueprint. It is the
edit-side counterpart to `docs/agent_call_contract.md` (read/analyze side).

The capability is implemented by:
- C++ engine: `FBPATEdit` (`BPATEdit.cpp`) + commandlet `UBPATEditCommandlet` (`-run=BPATEdit`).
- PowerShell wrapper: `scripts/edit_blueprint.ps1`.
- Self-test harness: `scripts/atomic_edit_selftest.ps1`.

The editor must be **closed** for `apply` / `apply-and-verify` (it locks assets and clobbers saves).
Edits are **non-destructive by default**: destructive operations are refused unless
`AllowDestructiveEdit` is set (you can still preview them with `plan-only` / `dry-run`).

---

## 1. Entry points

### PowerShell (preferred)

```powershell
.\scripts\edit_blueprint.ps1 `
  -UERoot "D:\software\UE\UE_5.4" `
  -ProjectUProject "E:\BPTestProject\BPTest\BPTest.uproject" `
  -AssetPath "/Game/BPParserTest/BP_04_ExecFlow_Control" `
  -EditRequestJson ".\request_001.json" `
  -OutputDir "<Project>\Saved\BPParserAgentReports" `
  -Mode apply-and-verify `       # plan-only | dry-run | apply | apply-and-verify
  -CreateBackup `                # switch
  -AllowDestructiveEdit `        # switch (required for destructive ops in apply modes)
  -WorkOnCopy "/Game/Scratch/Copy"   # optional: edit a duplicate, leave the source untouched
```

### Commandlet (direct)

```powershell
& "<UE>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<Project>.uproject" -run=BPATEdit `
  -AssetPath="/Game/..." -EditRequestJson="...json" -OutputDir="..." `
  -Mode=apply-and-verify -CreateBackup=1 -AllowDestructiveEdit=0 -Strict=0 `
  [-WorkOnCopy=/Game/Scratch/Copy] -unattended -nopause -nop4 -stdout
```

### Exit codes (process + `edit_result.json.status`)

| exit | status | meaning |
|------|--------|---------|
| 0  | `success`      | all ops applied; compiled; saved (apply modes) or plan produced (plan/dry) |
| 10 | `partial`      | refused (e.g. destructive op without permission) or saved-with-warnings |
| 20 | `failed`       | bad setup (asset/copy load failed) |
| 30 | `bad_input`    | missing/invalid request JSON or asset path |
| 40 | `rolled_back`  | an op failed or compile errored → **changes discarded, source unchanged** |

---

## 2. Edit request JSON

```json
{
  "schema_version": "1.0",
  "asset_path": "/Game/BPParserTest/BP_04_ExecFlow_Control",
  "intent": "human-readable description",
  "mode": "apply-and-verify",
  "allow_destructive_edit": false,
  "create_backup": true,
  "operations": [ { "op_id": "op_001", "operation": "...", "graph": "EventGraph", ... } ]
}
```
`-Mode` / `-AllowDestructiveEdit` / `-AssetPath` on the CLI override the JSON fields.

### Node selectors (used by `node`, `from_node`, `to_node`)

A selector matches **one** node in the target graph. Combine criteria to make it unique:

| key | meaning |
|-----|---------|
| `node_id` | exact NodeGuid (digits) — most precise, but project-specific |
| `node_class` | e.g. `K2Node_CallFunction`, `K2Node_Event`, `K2Node_VariableSet` |
| `node_title` | exact ListView title (case-insensitive) |
| `node_title_contains` | substring of the ListView title |
| `function_name` | for `K2Node_CallFunction`: the member name, e.g. `PrintString` |
| `exec_out_connected` / `exec_in_connected` | `true`/`false` — **disambiguates ghost Event stubs** (UE adds disabled `BeginPlay`/`Tick`/`ActorBeginOverlap` placeholders to Actor EventGraphs; the real wired event has its exec connected) |
| `match_index` | pick the Nth match when a selector is intentionally non-unique |

If a selector matches **0** nodes the op fails; if it matches **>1** and no `match_index` is given,
the op fails with `ambiguous selector` (a safety guard — never guess).

---

## 3. Operation catalog

Safe (allowed in any mode):
- `set_pin_default_value` — `{ node, pin, value }`
- `connect_pins` — `{ from_node, from_pin, to_node, to_pin }` (auto-inserts an int→string-style cast node when types differ but are convertible; verifies the link)
- `add_node` — `{ new_node }`
- `add_variable` — `{ var_name, var_type:{category[,sub_category,object_type,container_type]}, default_value?, category?, instance_editable? }`
- `set_variable_default` — `{ var_name, default_value }`
- `set_parent_class` (alias `reparent_blueprint`) — `{ new_parent_class, options?{create_backup,compile,rollback_on_failure} }` (change the Blueprint's parent; see §3b)

Widget Blueprint tree edits (safe; reuse the create-side reflection — see §3c):
- `set_widget_property` — `{ widget, property, value }` (fuzzy name resolution; emits `property_alias_matched`)
- `set_slot_property` — `{ widget, property, value }` (the widget's slot; e.g. `Position`/`Size`/`Padding`)
- `add_widget` — `{ parent, widget:{ name, type, properties?, slot?{properties} } }` (`type` = native short name / `/Script/...` / custom `/Game/...` UserWidget)
- `bind_widget_event` — `{ widget, event, handler?{ type, name, create_if_missing, connect_exec, connect_parameters, body } }`

Destructive Widget edits (need `AllowDestructiveEdit` in apply modes):
- `remove_widget` — `{ widget }` (cannot remove the root)
- `move_widget` — `{ widget, new_parent }` (reparent within the WidgetTree; `new_parent` must be a panel)

Destructive (need `AllowDestructiveEdit` in apply modes; freely previewable in plan/dry):
- `disconnect_pins` — `{ from_node, from_pin, to_node, to_pin }`
- `insert_node_between` — `{ from_node, to_node[, from_pin, to_pin], new_node }` (splices an exec edge: break A→B, connect A→New→B)
- `remove_node` — `{ node, preserve_exec:true|false }` (when preserving, reconnects predecessor→successor exec)
- `add_reroute_on_edge` — `{ from_node, from_pin, to_node, to_pin }` (inserts a Knot, preserves the logical link)

`new_node` factory supports: `K2Node_IfThenElse`, `K2Node_ExecutionSequence` (`num_outputs`),
`K2Node_Knot`, `K2Node_VariableGet`/`K2Node_VariableSet` (`variable_name`),
`K2Node_CallFunction` (`function_name` + `member_parent` class path), `UEdGraphNode_Comment` (`comment`,`w`,`h`),
each with an optional `position:{x,y}`.

---

## 3b. Blueprint Reparent / `set_parent_class`

Change an **existing** Blueprint's parent class safely. This is distinct from create-time `asset.parent_class`
(which only sets the parent when the asset is first made) — `set_parent_class` re-parents an already-existing asset,
refreshes its nodes, recompiles, and rolls back if the result does not compile.

```json
{ "operation": "set_parent_class",
  "new_parent_class": "/Script/AClient.RGUserWidget",
  "options": { "create_backup": true, "compile": true, "save": true, "rollback_on_failure": true } }
```
Alias: `reparent_blueprint`. `options` default to `compile=true`, `rollback_on_failure=true` (safe in any mode).

**`new_parent_class` formats (resolved to a `UClass`):**
- C++ class: `/Script/Module.Class` (e.g. `/Script/Engine.Pawn`, `/Script/UMG.UserWidget`, `/Script/AClient.RGUserWidget`).
- Blueprint generated class: `/Game/Path/BP_Base.BP_Base_C` (preferred), `/Game/Path/BP_Base.BP_Base`, or `/Game/Path/BP_Base` (all normalized to `_C`).
- On failure: op fails with `parent_class_load_failed` + `suggestion` (use `/Script/Module.Class` or `/Game/.../BP.BP_C`).

**Supported / unsupported blueprint kinds:**
- Supported: Actor BP → Actor/child/C++ Actor subclass; ActorComponent BP → ActorComponent subclass; Widget BP →
  UserWidget/C++ UserWidget subclass/WBP generated class; normal Blueprint → a Blueprintable class in the same family.
- Rejected (explicit fail, asset untouched): Interface, MacroLibrary, FunctionLibrary, LevelScript, AnimBlueprint.

**Safety validation (all before any mutation):** asset exists; current parent readable; new parent loads as
`UClass`; not self; no circular inheritance (new parent must not derive from this BP); allowed as a Blueprint parent
(`CanCreateBlueprintOfClass`); **family compatibility** — the new parent must stay in the blueprint's class family
(WBP→Actor, Actor→Component, etc. fail with `incompatible_parent_type`); backup/rollback planned.

**Execution & rollback:** record `old_parent_class` → set `ParentClass` → `RefreshAllNodes` +
`MarkBlueprintAsStructurallyModified` → compile. If compile fails and `rollback_on_failure` (default), the old parent
is restored (op fails → the framework does not save → on-disk asset unchanged). `edit_result.json` gains top-level
`operation`, `old_parent_class`, `new_parent_class`, `new_parent_source` (`cpp|blueprint`), `compile_status`,
`rollback_performed`; `diff_report.json` gains `modified_asset.parent_class{before,after}` + `risk_notes`.
Failure classes: `parent_class_load_failed | unsupported_blueprint_type | parent_is_self | circular_inheritance |
parent_not_blueprintable | incompatible_parent_type` + compile-failure rollback.

---

## 3c. Widget Blueprint tree edits (edit an existing WBP)

Edit the WidgetTree of an **existing** `UWidgetBlueprint` — the same capabilities as create, but applied to a loaded
asset, wrapped in the backup / compile-verify / rollback framework. Widgets are addressed by their `name` (as shown
in the analyze `widget_tree`); properties should use the internal names from each widget's `settable_properties`
(the ops apply the same alias resolution as create, and non-exact matches emit `property_alias_matched`).

- `set_widget_property` / `set_slot_property` — reflection write on the widget or its slot object.
- `add_widget` — construct a native or custom UserWidget under a panel `parent`, with optional `properties` + `slot.properties`.
- `remove_widget` (destructive) — remove a non-root widget; `move_widget` (destructive) — reparent within the tree.
- `bind_widget_event` — bind a widget delegate on the existing WBP and (optionally) create/reuse the handler
  (`custom_event`/`function`), wire exec + params, and generate a `body` (`print_string`/`set_text`). Self-contained
  compiles run as needed so a just-added widget's variable and a new handler's UFunction exist before wiring.

**CanvasPanelSlot anchor-aware geometry**: `set_slot_property`/`add_widget` guard `Position`/`Size` on a
**stretched** axis (`Anchors.Minimum != Maximum`), where `Offsets` are margins not size. Such a write is
`skipped` (non-fatal) with a `canvas_slot_stretch_axis_size_warning` (`axis`,`input_property`,`reason`,`suggestion`)
so a margin is never silently overwritten. Set stretched axes with `property:"Offsets" {Left,Top,Right,Bottom}` (or
`"LayoutData"`); force `Position`/`Size` with `allow_stretch_axis_size_override:true` (applies + warns). Analyze
`slot.geometry_semantics` to know which axis is stretched. `diff_report` decomposes `LayoutData` into semantic
components (`LayoutData.Offsets.Bottom … semantic=bottom_margin`) and flags large stretch-axis margin deltas in
`risk_notes`. See `docs/widget_blueprint_schema.md` "CanvasPanelSlot anchor-aware geometry editing".

The edit `diff_report.json` gains widget-aware categories (all always present, empty when unchanged):
`added_widgets`, `removed_widgets`, `moved_widgets`, `modified_widget_properties`, `modified_slot_properties`,
`added_event_bindings`, `modified_event_handlers`, plus `modified_variables`, `modified_functions`,
`modified_parent_class`, and the graph `added/removed_nodes` / `added/removed_edges`. For WBPs the edit output also
writes `viz/hierarchy.before.dot` and `viz/hierarchy.after.dot` (widget-hierarchy preview) alongside the graph
`before.dot`/`after.dot`. Note: `moved_widgets`/`removed_widgets` reflect the before→after *state* — a widget that
is added and then moved/removed in the same batch nets into `added_widgets` (or nothing), not `moved/removed`.

Verified (UE 5.4, AClient): a 10-op combined edit on a `/Game/Generated` copy of `WBP_Settings_Graphics`
(set widget Details + slot; add native TextBlock/Button; add custom `WBP_Setting_CheckboxItem`; bind
`Button.OnClicked` → custom event with a body; remove + move) — all ops success, compile up_to_date, saved,
0 unexpected changes; a follow-up illegal reparent rolled back with the copy unchanged; the source asset was never
modified.

---

## 4. Safe edit ordering (enforced by the engine)

- **Exec splice** (`insert_node_between`): find A→B edge → break it → connect A→New, New→B → verify. Never leaves the old edge dangling.
- **Node removal** (`remove_node` + `preserve_exec`): record predecessor/successor exec pins → break all node links → remove → reconnect predecessor→successor.
- **Input-pin single-link**: `connect_pins`/splice rely on the K2 schema, which replaces an input pin's existing link and auto-inserts conversion nodes for convertible types.
- **Reroute**: `add_reroute_on_edge` preserves the logical connection while inserting a Knot.

---

## 5. Output artifacts

Written to `Saved/BPParserAgentReports/<sanitized_asset>/edits/<timestampZ>/`:

```
edit_request.json   edit_plan.json    edit_result.json   diff_report.json
baseline_ir.json    modified_ir.json  summary.md         logs/edit_log.txt
viz/before.dot      viz/after.dot     viz/diff.mmd
```

`edit_result.json` (key fields): `status`, `mode`, `backup{created,path}`, `operations[]{op_id,operation,status,errors,warnings,outputs}`, `validation{compile_status,save_status,ir_redump_status,rolled_back}`, `diff`, `plan`, `artifacts{...}`.

`diff_report.json`: `added_nodes`, `removed_nodes`, `modified_nodes`, `added_edges`, `removed_edges`, `modified_pins`, `modified_variables`, `unexpected_changes`, `risk_notes`.

---

## 6. Backup & rollback

- With `-CreateBackup`, the edited asset is duplicated to `/Game/BPParserBackups/<Name>_<timestamp>` (saved) before any change.
- **Primary rollback mechanism:** the source asset is only saved on success. If any op fails or
  (`apply-and-verify`) the compile reports an error, the in-memory changes are **discarded without
  saving** — the on-disk asset is unchanged. `status` becomes `rolled_back` (exit 40).
- `-WorkOnCopy /Game/...` duplicates the source first and edits the copy, so the original is never
  touched regardless of outcome (used by the self-test to keep the suite pristine).

---

## 7. How another agent should consume results

1. Read process exit code → map to status (table in §1).
2. Open `edit_result.json`; check `status` and `validation.compile_status`.
3. On `success`: inspect `diff_report.json` to confirm only the intended changes occurred (`unexpected_changes` must be empty).
4. On `rolled_back`/`failed`/`partial`: read `operations[].errors` for the cause; the asset is unchanged.
5. Ask the user to confirm visually in UE (the editor must be reopened after edits).

---

## 8. Self-test

```powershell
.\scripts\atomic_edit_selftest.ps1 -UERoot "<UE>" -ProjectUProject "<...>.uproject"
```
Runs 8 isolated cases on copies of a stable source (insert / set-default / add-variable /
remove+preserve / add-reroute / destructive-refusal / plan-only-preview / rollback-on-failure) and
writes `selftest_summary.json`. Exit 0 = all passed. A tracked snapshot of the last run is at
`bpparser_testgen/deliverables/atomic_edit_selftest_summary.json`.

---

## 8b. Widget (UMG) property edits — settable_properties and name resolution

When an edit sets a **widget or slot property** (a `set_property` op on a `UWidgetBlueprint`, or a widget
property inside a create/edit `properties` object), the property key is resolved by the shared reflection
resolver (same as create):

- **exact → case-insensitive → bool `b` prefix → Details DisplayName → strip spaces/underscores**, then a
  `Set<Key>` setter UFUNCTION fallback.
- An aliased write is recorded as `property_alias_matched` (`input`→`resolved_to`); a miss is
  `property_not_found` **with a `suggestions` list** (`{name,display_name,type}`) — both in `property_notes[]`,
  never silent.

**Don't guess field names.** The Details DisplayName can differ from the real `FProperty` name, and bool
properties often carry a `b` prefix (Details `Default Checked` → property `bDefaultChecked`). The reliable flow:

```text
1. analyze the target WBP / custom control (task_type=analyze);
2. read each widget's settable_properties (+ slot_settable_properties) — use the internal `name`;
3. build the edit request with real names;
4. apply; then redump and confirm current_value changed.
```

`settable_properties` (internal name, DisplayName, type, declaring_class, editable/BP-visible/read-only,
deprecated, `current_value`, `set_supported`, `notes`) is documented in `docs/blueprint_ir_schema.md` §10.1;
the resolution rules and rationale are in `docs/issue_patterns.md` P16.

### Widget event handler wiring (Phase 4 P2)

Widget event binding **and** handler exec/data wiring is implemented (currently exercised through the `create`
path; see `docs/agent_create_contract.md` and `docs/widget_blueprint_schema.md`). An `events[]` entry with a
`handler` (`bound_event` | `custom_event` | `function`, plus `create_if_missing`/`connect_exec`/`connect_parameters`)
creates/reuses the handler entry, connects the bound-event exec first, then matches data params by name→type. All
outcomes are classified in `widget_event_bindings[].handler` (`connected`/`exec_connected`/`parameters_connected[]`
+ failure codes `handler_not_found|function_is_pure|exec_connection_failed|parameter_*`) and re-derivable from the
redumped graph. The wiring is idempotent — re-applying reuses the bound event, the custom event/function, and the
call node without duplicating links. Redump (`blueprint_ir_schema.md` §10.2) exposes the same `handler` flow so a
diff can confirm exec + parameter edges. Handler *body logic* is not auto-generated.

---

## 9. Known limitations (need manual UE confirmation or future work)

- `new_node` factory covers the common node families above; other node classes return a clear
  "unsupported new_node.node_class" error (extend `MakeNode` as needed).
- `map` variable container type is not yet wired in `add_variable` (needs key+value spec).
- Delegate/dispatcher binding edits and wildcard-container stabilization are planned (the read/gen
  side already has helpers in `FBPGen`; the edit ops are not yet exposed).
- `compare_ir` strictness vs. engine auto-inserted nodes (autocast/macro-expansion) should be
  reviewed per use case; `diff_report` reports the literal node/edge delta.
- After any apply, **reopen the asset in UE** to visually confirm.
