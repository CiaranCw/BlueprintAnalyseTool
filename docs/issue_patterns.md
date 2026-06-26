# Issue Patterns (Blueprint generation)

Reusable knowledge base of generation issue classes. Each pattern: symptom →
affected node families → root cause → generalized fix → validation → regression assets.
New patterns must be appended here whenever a local-UE issue is diagnosed (AGENTS.md §9).

---

## P1: PostPlacedNewNode resets node state set before it (node lifecycle ordering)

### Typical symptom
- All Comment boxes show the title "Comment" even though the generator set real text.
- More generally: a property written on a freshly-created node is silently lost.

### Affected node families
- `UEdGraphNode_Comment` (NodeComment).
- Any `ConstructObjectFromClass`-derived node (e.g. `UK2Node_SpawnActorFromClass`) whose
  `PostPlacedNewNode()` touches pins (asserts if pins not yet allocated).

### Root cause
`UEdGraphNode_Comment::PostPlacedNewNode()` hard-sets `NodeComment = "Comment"`.
The generator set `NodeComment` *before* calling `PostPlacedNewNode()`, so it was overwritten.
General rule: `PostPlacedNewNode()` may initialize/reset node state — any property that it
initializes must be applied AFTER it (and for ConstructObject nodes, `AllocateDefaultPins()`
must run before `PostPlacedNewNode()`).

### Generalized fix
- Centralize node lifecycle. For comments: `AddNode → CreateNewGuid → PostPlacedNewNode → set NodeComment/size`.
- For ConstructObjectFromClass nodes: `AllocateDefaultPins → PostPlacedNewNode` (see `SpawnActorNode`).
- File: `BPGen.cpp` (`AddComment`, `SpawnActorNode`).

### Validation
- Re-generate (editor closed), `export_ir`, confirm `graphs[].comments[].text` equals intended titles (not "Comment").

### Regression assets
- All BP_01..BP_11, BP_99 (every test BP has comment boxes); BP_09 specifically (incl. an intentionally empty comment → must stay empty, not "Comment").

---

## P2: Stale asset from editor clobber (regen workflow)

### Typical symptom
- UE shows old/broken structure (e.g. MakeStruct orphan pins, plain CallFunction array)
  even though the generator was fixed and `generation_log.json` says compiled OK.

### Root cause
The UE editor was open during generation; on its next save/auto-save it wrote its old
in-memory copy back to disk, clobbering freshly generated assets. (Evidence: asset mtime
later than `generation_log.json`.)

### Generalized fix
- `run_generate.ps1` / `local_acceptance.ps1`: refuse to run if `UnrealEditor` is running;
  clean `/Game/BPParserTest` before generating (deterministic). `-PluginSource` syncs repo→project.
- Doc: regen workflow note in `manual_check_guide.md`.

### Validation
- Close editor → clean regen → `export_ir` reflects the fixed structure.

---

## P3: Wildcard container nodes not type-stabilized (Array)

### Typical symptom
- "The type of Target Array / New Item is undetermined. Connect something to Add."

### Affected node families
- `UKismetArrayLibrary` wildcard funcs (Array_Add/Get/Length/RemoveItem/...).

### Root cause
Array library functions must be spawned as `UK2Node_CallArrayFunction` (not plain
`UK2Node_CallFunction`) so the connected array's element type propagates to wildcard pins.
(Set/Map plain `UK2Node_CallFunction` resolve via metadata once the container var is connected.)

### Generalized fix
- `FBPGen::SpawnCallArrayFunc` (spawns `UK2Node_CallArrayFunction`); connect a strongly-typed
  container variable Get to TargetArray before setting item defaults. Files: `BPGen.cpp`, builders.

### Regression assets
- BP_02_StructEnumContainers, BP_08_ComplexGameplayLikeGraph, BP_11_SupplementalCoverage.

---

## P4: AutoCreateRefTerm (by-ref) struct pins need a real input

### Typical symptom
- "Spawn Transform must connect an input (by ref param needs a valid input)."

### Root cause
`UK2Node_SpawnActorFromClass` SpawnTransform is AutoCreateRefTerm; an empty literal fails.

### Generalized fix
- Feed a `MakeTransform` into the SpawnTransform pin if unconnected (`SpawnActorNode`).

### Regression assets
- BP_03, BP_08, BP_10.

---

## P5: MakeStruct/BreakStruct field pins are name-suffixed

### Typical symptom
- Struct field defaults (e.g. ID/Score) silently not applied.

### Root cause
UserDefinedStruct member pins are named `Field_<index>_<GUID>`, so `FindPin("ID")` fails.

### Generalized fix
- `FBPGen::SetStructPinDefault` / `FindPinByPrefix` match the friendly-name prefix. Files: `BPGen.cpp`, builders.

### Regression assets
- BP_02, BP_08, BP_10.

---

## P7: Exec chaining mis-wired through multi-exec-output nodes

### Typical symptom
- Switch case `Idle` wrongly drives the next node; ForEach `Loop Body` drives the post-loop node;
  `Completed`/intended branch left unconnected.

### Affected node families
- Any node with multiple exec OUTPUTs: Switch (Int/String/Enum), ForEach/loop macros, Branch,
  FlipFlop, Gate, Sequence, DoOnce.

### Root cause
A linear "Tail" cursor + `ConnectExec(From,To)` picked `FindExecOut(From)` = the FIRST exec output,
which on multi-exec nodes is a case/loop-body pin, not the continuation. Also: an exec OUTPUT is
1:1 in UE, so a single node cannot fan to two paths — a Sequence is required.

### Generalized fix
- `ConnectExec` now prefers the "then" pin and REFUSES (warns) when a node has multiple exec
  outputs and no "then" (so mis-wiring fails loudly). New `ConnectExecFrom(From,"<pin>",To)` for
  named multi-exec outputs. Fan-out uses an explicit Sequence. Files: `BPGen.cpp`, builders BP_02/04/11.

### Validation
- `export_ir` + read exec edges by (node_title.pin_name -> node_title); assert continuation pins
  (Completed/Then1) drive the next node, not case/body pins.

### Regression assets
- BP_02 (Switch + ForEach), BP_04 (all StandardMacros), BP_11 (ForEachLoopWithBreak).

---

## P8: Switch-on-Enum case pin uses internal name; macro pins differ by spacing

### Typical symptom
- Connecting a switch case by display name ("Moving") silently fails; ForEach "Loop Body" not connected.

### Root cause
- SwitchEnum case pins are named by the enum's INTERNAL name (`NewEnumerator1`), while the editor
  shows the display name ("Moving"). StandardMacros body pin is `LoopBody` (no space), not "Loop Body".

### Generalized fix
- `ConnectEnumCase(switch, enum, displayName, to)` resolves display name -> enum index -> case pin
  (with Nth-exec-output fallback). `ConnectExecFrom` matches pin names space/case-insensitively.
  Files: `BPGen.cpp`, builders.

### Validation
- `export_ir`; confirm `Switch.NewEnumerator1 -> Print` and `ForEach.LoopBody -> Print` edges exist.

### Regression assets
- BP_02 (enum case), BP_04 / BP_11 (macro body pins).

---

## P11: Macro graph created with empty body / duplicate exec pin name ("Exec 2")

### Typical symptom
- A user macro's Input and Output tunnel nodes are disconnected (macro does nothing); the output
  exec pin shows "Exec 2" instead of the intended name.

### Affected node families
- Any generated macro graph (`AddMacroGraph`) and its `K2Node_Tunnel` input/output nodes; the
  unique-name rule applies to ALL macro signature pins (exec + data).

### Root cause
- (1) The builder created only the macro signature (tunnels + user pins) and left the body unwired
  (an earlier "tunnel wiring is fragile" decision) → coverage gap. (2) Macro signature pin names
  must be unique across the whole instance; adding an input exec "Exec" AND an output exec "Exec"
  makes UE auto-rename the second to "Exec 2".

### Generalized fix
- Reusable tunnel accessors `FBPGen::GetMacroInputTunnel` / `GetMacroOutputTunnel` (input tunnel =
  `bCanHaveOutputs`, output tunnel = `bCanHaveInputs`) let builders wire macro bodies. Name input
  and output exec distinctly (e.g. input "Exec", output "Out"). BP_05's `Macro_LogWithPrefix` body
  is now auto-wired: Concat(Prefix,Message) -> PrintString.InString; In exec -> Print -> Out exec.
  Files: `BPGen.cpp/.h`, `BPGenBuilders_A.cpp`.

### Validation
- `export_ir`; assert the macro graph has body nodes (Concat_StrStr, PrintString), 5 edges, and the
  output exec pin is "Out" (not "Exec 2"). Blueprint compiles clean.

### Regression assets
- BP_05_Functions_Macros_LocalVariables (Macro_LogWithPrefix).

---

## P9: Loop/value data output not wired to consumer (and int→string needs an autocast node)

### Typical symptom
- A loop prints a static placeholder ("Hello"/"Element"/"elem") instead of the live value:
  `ForLoop.Index` / `ForEach."Array Element"` never reach `PrintString.InString`. The exec wire
  exists but the **data** wire is missing, so the consumer falls back to its pin default.

### Affected node families
- Any value OUTPUT feeding a typed INPUT, especially loop counters/elements into PrintString,
  Append, Format Text; and ANY connection where source/target categories differ but are
  convertible (int/float/byte/name/bool → string, etc.).

### Root cause
- Two layers: (1) the builder only wired exec and forgot the data edge; (2) `Index`(int) and
  `Array Element`(int) are not directly assignable to `InString`(string) — the editor inserts a
  conversion ("To String (Integer)" = `Conv_IntToString`) on a manual drag. Programmatically the
  data edge must be created AND the conversion handled.

### Generalized fix
- `FBPGen::ConnectData(From,"<outPin>",To,"<inPin>")` routes through `UEdGraphSchema_K2::TryCreateConnection`,
  which the engine resolves via `CanCreateConnection` → `CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE`
  → `CreateAutomaticConversionNodeAndConnections` (verified in `EdGraphSchema.cpp`). So no manual
  Conv node is needed — the schema inserts it. `ConnectData` also VERIFIES both endpoints became
  linked and warns otherwise. Files: `BPGen.cpp/.h`, builders BP_02/04/11.

### Validation
- `export_ir`; assert a `To String (Integer)` node exists between the loop value pin and the print,
  and that the print's `InString.is_connected == true`. Confirm blueprint compiles clean.

### Regression assets
- BP_04 (ForLoop.Index→Print, ForEach."Array Element"→Print: 2 autocast nodes),
  BP_02 / BP_11 (ForEach element→Print: 1 autocast node each).

---

## P10: Pin default / lookup silently fails on display-vs-internal name spacing

### Typical symptom
- `SetPinDefault(ForLoop,"Last Index","3")` has no effect — the pin still shows 0. The call
  returned without error because `FindPin` found nothing and the default was never applied.

### Affected node families
- StandardMacros whose internal pin names drop spaces inconsistently: ForLoop `FirstIndex`/`LastIndex`/
  `Index`, ForEach `LoopBody` (no space) vs `Array Element`/`Array Index` (with space). Also tolerates
  minor pin-name drift across UE 5.x.

### Root cause
- `FindPin` matched the pin name exactly (case-insensitive). Callers passed the friendly display
  name ("First Index") which does not equal the internal name ("FirstIndex"), so lookup—and the
  dependent `SetPinDefault`—silently no-op'd.

### Generalized fix
- `FindPin` now does a second pass with a normalized comparison (strip spaces + underscores,
  case-insensitive). Every lookup-based helper (`SetPinDefault`, `OutPin/InPin`, `ConnectData`)
  inherits the tolerance. Files: `BPGen.cpp`.

### Validation
- `export_ir`; assert `ForLoop.LastIndex.default_value == "3"`. (`FirstIndex` stays empty = the
  engine's "0" autodefault, which displays as 0.)

### Regression assets
- BP_04 (ForLoop First/Last Index defaults).

---

## P6: MinimalAPI node methods not linkable cross-module

### Typical symptom
- LNK2019 unresolved `UK2Node_SwitchEnum::SetEnum`.

### Root cause
`UCLASS(MinimalAPI)` does not export arbitrary methods. Set the public field directly
(`N->Enum = Enum;`); `CreateCasePins()` calls `SetEnum` internally to build case pins.

### Generalized fix
- Prefer public UPROPERTY assignment over non-exported methods for MinimalAPI K2 nodes.
