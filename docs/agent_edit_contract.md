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
