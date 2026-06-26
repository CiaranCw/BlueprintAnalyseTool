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

## P6: MinimalAPI node methods not linkable cross-module

### Typical symptom
- LNK2019 unresolved `UK2Node_SwitchEnum::SetEnum`.

### Root cause
`UCLASS(MinimalAPI)` does not export arbitrary methods. Set the public field directly
(`N->Enum = Enum;`); `CreateCasePins()` calls `SetEnum` internally to build case pins.

### Generalized fix
- Prefer public UPROPERTY assignment over non-exported methods for MinimalAPI K2 nodes.
