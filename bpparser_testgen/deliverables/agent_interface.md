# Agent-callable interface (CLI + JSON contract)

All core flows are command-line, parameterized (no hardcoded paths), and emit
machine-readable JSON. Designed for Claude Code CLI / Cursor Agent / CI.

> Standard params across scripts: `-UERoot` (auto-discovered if omitted),
> `-ProjectUProject` (full path to `.uproject`), `-OutputDir`.
> `build_plugin.ps1` / `run_generate.ps1` also accept legacy aliases `-UE_ROOT` / `-PROJECT_UPROJECT`.

## Scripts

| Script | Purpose | Key params | Exit codes | Outputs |
|---|---|---|---|---|
| `scripts/local_acceptance.ps1` | One-stop: discover UE → ensure code project → build → generate → validate → manifest | `-UERoot? -ProjectUProject -OutputDir? -SkipBuild -SkipGenerate -SkipViz -Strict` | 0 success / 10 partial / 20 failed | `acceptance_manifest.json` + the 3 below |
| `scripts/ensure_code_project.ps1` | Make a Blueprint-only project buildable (generic; derives module name from `.uproject`) | `-ProjectUProject -PluginNames` | 0 / 30 | creates `Source/`, edits `.uproject` (+`.bak`) |
| `scripts/build_plugin.ps1` | Compile `<Project>Editor` | `-UERoot -ProjectUProject` | 0 / 1 | UBT build, `Saved/Logs` |
| `scripts/run_generate.ps1` | Run `-run=BPParserTestGen` | `-UERoot -ProjectUProject` | 0 / 1 | `generation_log.json` |
| `scripts/validate_outputs.ps1` | Validate expected_ir JSON + edges + viz | `-OutputDir -GenerationLog? -ExpectedIrDir? -VizDir?` | 0 / 1 (hard fail) | `coverage_summary.json`, `failed_items.json`, `manual_check_required.json` |
| `scripts/export_ir.ps1` | Dump one blueprint's real IR (`-run=BPParserTestDump`) | `-UERoot? -ProjectUProject -AssetPath -OutputDir?` | 0 / 30 / 50 | `<OutputDir>/ir_dumps/<name>.ir.json` |
| `scripts/compare_ir.ps1` | Structural diff expected vs actual IR | `-ExpectedJson -ActualJson -OutDiff?` | 0 (no diff) / 5 (diff) / 30 | `<name>.diff.json` |
| `scripts/render_viz.ps1` | Render DOT/Mermaid → PNG/SVG (needs Graphviz/Mermaid) | `-VIZ_DIR? -OUT_DIR?` | 0 / 2 (no renderer) | `viz/*.png,*.svg` |

## Canonical one-shot call

```powershell
.\scripts\local_acceptance.ps1 -ProjectUProject "E:\BPTestProject\BPTest\BPTest.uproject"
# or explicit engine:
.\scripts\local_acceptance.ps1 -UERoot "C:\Program Files\Epic Games\UE_5.4" `
  -ProjectUProject "E:\BPTestProject\BPTest\BPTest.uproject" `
  -OutputDir "E:\BPTestProject\BPTest\Saved\BPParserTestReports"
```

## acceptance_manifest.json (shape)

```json
{
  "schema_version": "1.0",
  "status": "success|partial|failed",
  "ue_version": "5.4", "ue_root": "...", "project_uproject": "...", "project": "BPTest",
  "plugin_loaded": true, "build_success": true, "generation_success": true,
  "generated_assets": ["/Game/BPParserTest/..."],
  "failed_assets": [],
  "total_assets": 17, "failed_count": 0, "manual_check_count": 10,
  "steps": [ { "name": "build", "status": "success", "log": "...", "errors": [], "warnings": [] } ],
  "assets": [ { "name": "BP_01_PrimitivePins_Basic", "path": "...", "type": "Actor",
               "generated": true, "compiled": true, "compile_status": "up_to_date",
               "warnings": [], "errors": [], "manual_check_required": [] } ],
  "artifacts": { "generation_log": "...", "build_log": "...", "coverage_summary": "...",
                 "failed_items": "...", "manual_check_required": "...",
                 "expected_ir_dir": "...", "viz_dir": "...", "coverage_matrix": "..." }
}
```

- `status`: **success** = all green & nothing manual; **partial** = automation green but manual UE check remains (normal end state); **failed** = build/gen/validate failure.
- An Agent should branch on `status`, then read `failed_items.json` (fix) and `manual_check_required.json` (hand to a human / browser-use agent).

## Regression workflow for other Agents

```powershell
# 1. export real IR for a (possibly user-modified) blueprint
.\scripts\export_ir.ps1 -UERoot $UE -ProjectUProject $P -AssetPath "/Game/BPParserTest/BP_01_PrimitivePins_Basic"
# 2. diff against the design baseline
.\scripts\compare_ir.ps1 `
  -ExpectedJson "bpparser_testgen\deliverables\expected_ir\BP_01_PrimitivePins_Basic.json" `
  -ActualJson   "<...>\ir_dumps\BP_01_PrimitivePins_Basic.ir.json" `
  -OutDiff "<...>\BP_01.diff.json"   # exit 5 if structural differences
```

> `expected_ir/*.json` are **human-readable design baselines**. The dump is the **real parse**
> (`bpat-ir-dump-1.0`). For byte-stable regression, snapshot real dumps as golden once verified.

## IR dump commandlet (parser, self-contained)

```
UnrealEditor-Cmd.exe <Project>.uproject -run=BPParserTestDump \
   -AssetPath=/Game/BPParserTest/BP_01_PrimitivePins_Basic -OutputDir=<dir>
```
Implemented in `BPParserTestGen` (`BPGenIRDumper`), independent of the read-only
BlueprintAgentTools serializer (which is still a skeleton). Emits graphs / nodes /
pins / edges / variables / functions / macros / event_dispatchers / interfaces.

## Version generality
- UE auto-discovery: registry (`EpicGames\Unreal Engine\<ver>`) + common install roots + GUID builds.
- Version-risky APIs centralized in `BPGenUECompat.h` (SavePackage, engine version).
- `ensure_code_project.ps1` uses `BuildSettingsVersion.Latest` / `EngineIncludeOrderVersion.Latest`.
- Target UE **5.4**; designed to tolerate 5.3/5.5/5.6 with the compat header as the single edit point.
