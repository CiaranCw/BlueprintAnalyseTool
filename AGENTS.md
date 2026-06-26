# AGENTS.md

This repository contains an Unreal Engine Blueprint parser / Blueprint test generation agent.
All coding agents working in this repository must follow these rules.

## 1. Core Mission

This project is not a one-off test for a single Unreal Engine project.
It is a reusable agent/tooling system that should work across projects using the same UE version, and should be designed to tolerate common UE version differences when possible.

When fixing any issue, do not only patch the current failing blueprint, node, pin, or local project.
Always identify the general problem class and improve the generator, parser, validator, scripts, or documentation so the same type of issue is handled consistently in future projects.

## 2. Never Fake Success

Do not claim success unless it was actually verified.

Never claim:

- the UE plugin compiled unless the build command completed successfully;
- `.uasset` files were generated unless UE actually generated them;
- blueprints compile unless UE compile results or `generation_log.json` confirm it;
- PNG/SVG files were generated unless the files exist;
- expected IR is valid unless JSON validation passed;
- coverage is complete unless `coverage_matrix.md` and generated logic agree.

Always distinguish:

- executed and succeeded;
- executed and failed;
- fixed and revalidated;
- not executable in the current environment;
- pending local UE validation;
- requires manual inspection in the UE editor.

## 3. Generalization Requirement

For every warning, error, missing connection, bad pin type, broken node, invalid graph, or IR mismatch:

1. classify the issue type;
2. identify the direct cause;
3. identify the generator / parser / IR / visualization / documentation layer involved;
4. determine which node families or pin families may also be affected;
5. implement a generalized fix;
6. update regression checks or issue-pattern documentation;
7. update expected IR, visualization, coverage matrix, and manual check guide if needed.

Do not hardcode one blueprint name, one node name, one local path, or one project-specific workaround unless there is no other option. If a workaround is unavoidable, document it explicitly.

## 4. UE Blueprint Generation Rules

When modifying Blueprint generation logic, prefer reusable utilities for:

- node creation lifecycle;
- pin lookup;
- pin type validation;
- exec/data/delegate/object-reference connection validation;
- wildcard pin stabilization;
- macro instance creation;
- event dispatcher creation and binding;
- struct / enum / interface refresh;
- blueprint compile and save;
- expected IR consistency checks.

When creating K2 nodes, ensure the proper lifecycle is respected where applicable:

- set function / macro / delegate references before relying on pins;
- call node reconstruction or pin allocation when required;
- validate that required pins exist;
- validate that links are actually established after connection;
- record structured warnings and errors.

## 5. Known High-Risk Blueprint Areas

Treat the following as high-risk and validate carefully:

- StandardMacros: DoOnce, DoN, FlipFlop, Gate, ForLoop, ForLoopWithBreak, ForEachLoop, ForEachLoopWithBreak, WhileLoop;
- wildcard containers: Array, Set, Map, MakeArray, MakeSet, MakeMap, Select and similar wildcard nodes;
- delegates and event dispatchers: CreateDelegate, AddDelegate, ClearDelegate, CallDelegate, CustomEvent parameter matching;
- interface calls and target pins;
- Cast object input and valid / invalid exec outputs;
- latent nodes: Delay, Timer, Timeline, Async nodes;
- user-defined Struct, Enum, Interface assets;
- MakeStruct, BreakStruct, SetMembersInStruct;
- pure functions, macro graphs, function graphs, delegate signature graphs;
- reroute nodes and multi-exec convergence;
- unconnected pins with default values.

## 6. Required Machine-Readable Outputs

Any acceptance or repair workflow should produce or update machine-readable outputs when applicable:

- `Saved/BPParserTestReports/generation_log.json`
- `Saved/BPParserTestReports/acceptance_manifest.json`
- `Saved/BPParserTestReports/coverage_summary.json`
- `Saved/BPParserTestReports/failed_items.json`
- `Saved/BPParserTestReports/manual_check_required.json`
- `Saved/BPParserTestReports/fix_report_<timestamp>.json`

These files must be valid JSON and suitable for Claude Code CLI, Cursor Agent, or other coding agents to consume.

## 7. Local Acceptance Workflow

Before asking the user to inspect blueprints manually in UE, run or prepare the local acceptance workflow:

1. discover UE root and `.uproject`;
2. validate plugin structure;
3. build the plugin;
4. run the generator;
5. validate generated assets and logs;
6. validate expected IR JSON;
7. validate DOT / Mermaid visualization files;
8. update coverage and manual-check documents;
9. produce a final acceptance manifest.

Prefer using scripts under `scripts/`, especially:

- `scripts/local_acceptance.ps1`
- `scripts/build_plugin.ps1`
- `scripts/run_generate.ps1`
- `scripts/validate_outputs.ps1`
- `scripts/export_ir.ps1`
- `scripts/compare_ir.ps1`

Do not hardcode local user paths. Use parameters such as:

- `-UERoot`
- `-ProjectUProject`
- `-OutputDir`
- `-PluginRoot`

## 8. When a Local UE Validation Issue Is Reported

When the user reports a UE warning, compile error, missing wire, broken node, invalid pin, or mismatch with expected IR, follow this process:

1. restate the issue and available evidence;
2. read relevant logs, JSON, generated files, source code, and documentation;
3. classify the issue;
4. perform layered root-cause analysis:
   - direct UE symptom;
   - generator-layer cause;
   - parser-layer cause if relevant;
   - expected IR / visualization / docs mismatch if relevant;
   - UE version or API compatibility factor;
5. design a generalized fix;
6. modify code and docs;
7. update or add regression coverage;
8. rerun validation where possible;
9. generate a machine-readable fix report.

Do not only patch the current failing node.

## 9. Documentation Updates

When a new issue pattern is discovered, update:

- `docs/issue_patterns.md`

Each issue pattern should include:

- typical symptom;
- affected node families;
- root cause;
- generalized fix;
- validation method;
- regression test assets.

If coverage changes, update:

- `coverage_matrix.md`
- `manual_check_guide.md`
- `regression_protocol.md`
- relevant expected IR files;
- relevant DOT / Mermaid files.

## 10. Project Safety

Do not delete user assets, UE project content, or generated blueprints unless explicitly instructed.
Do not overwrite user-modified files without reporting the planned change.
Do not upload caches, build artifacts, logs, secrets, `.env`, or UE generated folders such as `Binaries/`, `Intermediate/`, `Saved/`, and `DerivedDataCache/`.

## 11. Before Finishing a Task

Before declaring a task complete, provide:

1. what was changed;
2. why it was changed;
3. which generalized issue class was addressed;
4. which files were modified;
5. which validation steps passed;
6. which steps remain pending UE/manual validation;
7. what the user should check next in the UE editor.

## 12. Task-Specific Prompt Routing

For specialized workflows, agents must read the corresponding prompt document before editing code or producing final reports.

Use these routing rules:

- Local UE validation issue or generated Blueprint warning/error:
  - read `docs/prompts/local_acceptance_issue_repair_prompt.md`
- Existing Blueprint understanding / AssetPath analysis / Blueprint structure dump:
  - read `docs/prompts/blueprint_understanding_validation_prompt.md`
- Blueprint modification / atomic edit / node insertion / node deletion / rewiring:
  - read `docs/prompts/blueprint_atomic_edit_validation_prompt.md`
- Local acceptance before user manually opens UE:
  - read `docs/prompts/final_local_acceptance_prompt.md` if present

If a referenced prompt file is missing, do not silently continue. Report the missing file and either create a suitable draft or ask the user whether to proceed with the available AGENTS.md rules only.

## 13. Existing Blueprint Understanding

When the user or another agent provides a Blueprint AssetPath and asks for analysis, structure extraction, visualization, or callable output, do not use expected IR as the source of truth.

The agent must:

1. load the real Blueprint asset through UE tooling, Commandlet, or the parser plugin;
2. output a valid `manifest.json`;
3. output a valid `blueprint_ir.json`;
4. output a human-readable `summary.md`;
5. output DOT and Mermaid visualization files;
6. clearly mark unsupported graph/node/pin types as `partial` or `manual_check_required`;
7. ensure the result is suitable for Claude Code CLI, Cursor Agent, or another agent to consume.

The main callable contract should be documented in:

- `docs/agent_call_contract.md`

Other agents should be able to call a script such as:

```powershell
.\scripts\analyze_blueprint.ps1 `
  -UERoot "<UE_ROOT>" `
  -ProjectUProject "<PROJECT_UPROJECT>" `
  -AssetPath "/Game/..." `
  -OutputDir "<OUTPUT_DIR>" `
  -Mode "full"
```

The primary output file for other agents is always:

```text
manifest.json
```

## 14. Atomic Blueprint Editing

When validating or implementing Blueprint edits requested by the user or another agent, read:

- `docs/prompts/blueprint_atomic_edit_validation_prompt.md`

Before applying any Blueprint edit, the agent must:

1. dump the current real Blueprint IR;
2. create a baseline;
3. generate an `edit_plan.json`;
4. perform precondition checks;
5. create a backup or rollback plan;
6. apply atomic operations in a safe order;
7. redump the modified IR;
8. compare before/after IR;
9. compile and save the Blueprint where possible;
10. output `edit_result.json` and `diff_report.json`.

No destructive edit may be applied unless explicitly allowed by the request or by `AllowDestructiveEdit=true`.

When editing Blueprint connections, remember that UE input pins may replace existing links depending on connection order. Therefore, the agent must explicitly handle:

- exec-chain splice order;
- data-link replacement order;
- wildcard pin stabilization;
- delegate/event pin matching;
- reroute/knot preservation or removal strategy;
- rollback on compile or verification failure.

The callable edit contract should be documented in:

- `docs/agent_edit_contract.md`

## 15. Shell / Tool Failure Fallback

If shell execution is unavailable, unstable, or returns no exit status, do not treat the task as impossible.

Instead:

1. report that command execution is currently unavailable;
2. continue with static file analysis where possible;
3. inspect source files, JSON, Markdown, and scripts directly;
4. generate patches or full replacement files;
5. list the exact commands the user should run locally;
6. mark validation status as `pending_local_execution`.

Do not claim build, generation, rendering, or UE validation success unless those commands actually completed successfully.

## 16. Agent Callable Contracts

This project must remain callable by other agents and automation tools.

Whenever adding or changing a workflow, ensure the workflow has:

1. a script or Commandlet entry point;
2. documented input parameters;
3. documented output files;
4. documented exit codes or status values;
5. machine-readable JSON output;
6. human-readable summary output;
7. clear failure categories;
8. examples for Claude Code CLI / Cursor Agent style usage.

Preferred status values are:

```text
success
partial
failed
rolled_back
skipped
pending_local_validation
```

Avoid ambiguous status values such as `ok`, `done`, `maybe`, or `finished`.

Before finishing any workflow-related task, update the corresponding contract document when applicable:

- `docs/agent_call_contract.md`
- `docs/agent_edit_contract.md`
- `docs/issue_patterns.md`
- `manual_check_guide.md`
- `regression_protocol.md`