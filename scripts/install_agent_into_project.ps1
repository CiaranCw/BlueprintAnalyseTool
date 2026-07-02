<#
  install_agent_into_project.ps1 - make the Blueprint Agent DISCOVERABLE and SELF-USABLE by the AI
  built into another project (Claude Code, Codex, Cursor, Gemini, ...).

  It writes the instruction files those tools auto-read, plus a machine-readable descriptor and example
  requests, so the target project's AI notices the agent and can drive it via one request.json.

  Idempotent + non-destructive: AGENTS.md / CLAUDE.md get a delimited MANAGED BLOCK (replaced on re-run,
  never clobbering surrounding content). The .cursor/.claude/tool files are ours and are overwritten.

  Usage:
    .\install_agent_into_project.ps1 -TargetDir "<project root>" [-ProjectUProject "<...>.uproject"] [-AgentRoot "<agent repo>"] [-Reference]

  Default: copies scripts/docs/plugin into <TargetDir>/Tools/BlueprintAgent (self-contained).
  -Reference: instead point the docs at -AgentRoot's absolute paths (no copy).
  Exit codes: 0 ok, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $TargetDir,
  [string] $ProjectUProject = "",
  [string] $AgentRoot = "",
  [switch] $Reference
)
$ErrorActionPreference='Stop'
if (-not (Test-Path $TargetDir)) { Write-Error "TargetDir not found: $TargetDir"; exit 30 }
if ([string]::IsNullOrWhiteSpace($AgentRoot)) { $AgentRoot = Split-Path $PSScriptRoot -Parent }
if (-not (Test-Path (Join-Path $AgentRoot 'scripts\blueprint_agent.ps1'))) { Write-Error "AgentRoot invalid (no scripts/blueprint_agent.ps1): $AgentRoot"; exit 30 }
if ([string]::IsNullOrWhiteSpace($ProjectUProject)) {
  $up = Get-ChildItem $TargetDir -Filter *.uproject -EA SilentlyContinue | Select-Object -First 1
  if ($up) { $ProjectUProject = $up.FullName }
}
$uprojForDoc = if ($ProjectUProject) { $ProjectUProject } else { "<PATH>/YourProject.uproject" }

# ---- resolve entry + plugin-source paths (copy vs reference) ----
$toolsDir = Join-Path $TargetDir 'Tools\BlueprintAgent'
if ($Reference) {
  $entry     = Join-Path $AgentRoot 'scripts\blueprint_agent.ps1'
  $docsPath  = Join-Path $AgentRoot 'docs'
  $pluginSrc = Join-Path $AgentRoot 'bpparser_testgen\Plugins\BPParserTestGen'
} else {
  New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
  robocopy (Join-Path $AgentRoot 'scripts') (Join-Path $toolsDir 'scripts') /E /NFL /NDL /NJH /NJS /NP | Out-Null
  robocopy (Join-Path $AgentRoot 'docs')    (Join-Path $toolsDir 'docs')    /E /NFL /NDL /NJH /NJS /NP | Out-Null
  robocopy (Join-Path $AgentRoot 'bpparser_testgen\Plugins\BPParserTestGen') (Join-Path $toolsDir 'plugin\BPParserTestGen') /E /XD Binaries Intermediate /NFL /NDL /NJH /NJS /NP | Out-Null
  $entry     = 'Tools\BlueprintAgent\scripts\blueprint_agent.ps1'
  $docsPath  = 'Tools/BlueprintAgent/docs'
  $pluginSrc = 'Tools\BlueprintAgent\plugin\BPParserTestGen'
}

function Expand([string]$t){ return $t.Replace('{{ENTRY}}',$entry).Replace('{{DOCS}}',$docsPath).Replace('{{UPROJECT}}',$uprojForDoc).Replace('{{PLUGIN_SRC}}',$pluginSrc) }

# ---- managed block (shared by AGENTS.md and CLAUDE.md) ----
$blockTmpl = @'
<!-- BEGIN BLUEPRINT-AGENT (managed by install_agent_into_project.ps1; do not edit between markers) -->
## Blueprint Agent — understand / edit / create UE Blueprints (AI-callable)

This project ships a **Blueprint Agent**: a tool to analyze, modify, and create Unreal Engine Blueprints
and Widget Blueprints. When you (an AI) are asked to understand a Blueprint's structure (graphs, nodes,
pins, connections, variables, functions), to modify one, or to create one, USE THIS AGENT — do not
hand-parse `.uasset` or guess from names.

### One entry point (PowerShell)
```
powershell -NoProfile -File "{{ENTRY}}" -RequestJson "<path to request.json>"
```
It takes ONE `request.json` and always writes a machine-readable manifest. Read the printed
`dispatch manifest` (and the sub `manifest.json`) to judge the result.

### Always start with a read-only probe (no build, no UE launch, safe)
request.json:
```json
{ "schema_version":"1.0", "task_type":"status", "project": { "uproject": "{{UPROJECT}}" } }
```
Then read `capability_state.json` (under `<project>/Saved/BPParserAgentReports/status/`):
`stage` (offline_only|python_only|needs_install|needs_build|native_ready),
`capabilities{understand_full,edit,create}`, `warmup_required`, `recommended_action`, `next_calls`.

### Decide
- `capabilities.understand_full == true` → go straight to analyze/edit/create with `execution.mode:"auto"`.
- `warmup_required == true` AND the user permits a one-time build → run `task_type:"warmup"` with
  `project.engine_policy.allow_incremental_compile:true` (installs a read-only plugin + incrementally
  builds the Editor against this project's engine; it NEVER modifies assets). Then proceed.
- Otherwise use `execution.mode:"python_partial"` or `"offline"` for PARTIAL understanding (no full
  graph). Never present a partial result as full.

### Task types
`status | warmup | analyze | edit | create | validate`. Request shapes and node/pin/edge schema:
see `{{DOCS}}/request_schemas.md`; machine descriptor: `Tools/BlueprintAgent/blueprint_agent.manifest.json`;
runnable examples: `Tools/BlueprintAgent/requests/`.

### Read results
Everything is in `manifest.json` / `dispatch_manifest.json`: `status` (success|partial|failed|
rolled_back|exists_refused), `mode`, `outputs`, `warnings`, `errors`, `manual_check_required`, `next_actions`.

### Safety
`status`/`analyze` are read-only. `warmup` builds (needs consent). `edit` needs `allow_destructive_edit`
for destructive ops and always keeps a backup/rollback. `create` obeys `overwrite_policy`
(fail_if_exists|create_unique_name|overwrite_if_allowed) and never overwrites by default.
<!-- END BLUEPRINT-AGENT (managed) -->
'@
$block = Expand $blockTmpl

function Upsert-ManagedBlock([string]$file,[string]$block){
  $begin='<!-- BEGIN BLUEPRINT-AGENT'; $end='<!-- END BLUEPRINT-AGENT (managed) -->'
  if (Test-Path $file) {
    $txt = Get-Content $file -Raw
    $bi = $txt.IndexOf($begin); 
    if ($bi -ge 0) {
      $ei = $txt.IndexOf($end); if ($ei -ge 0) { $ei += $end.Length
        $new = $txt.Substring(0,$bi) + $block + $txt.Substring($ei)
        Set-Content -Encoding UTF8 $file $new; return 'replaced'
      }
    }
    Add-Content -Encoding UTF8 $file ("`r`n`r`n" + $block); return 'appended'
  } else {
    $hdr = "# " + [IO.Path]::GetFileNameWithoutExtension($file) + "`r`n`r`n"
    Set-Content -Encoding UTF8 $file ($hdr + $block); return 'created'
  }
}

$written = New-Object System.Collections.ArrayList
[void]$written.Add(@{ file='AGENTS.md'; action=(Upsert-ManagedBlock (Join-Path $TargetDir 'AGENTS.md') $block) })
[void]$written.Add(@{ file='CLAUDE.md'; action=(Upsert-ManagedBlock (Join-Path $TargetDir 'CLAUDE.md') $block) })

# ---- Cursor rule ----
$mdc = Expand @'
---
description: UE Blueprint Agent - analyze/understand, edit, or create Unreal Blueprints & Widget Blueprints via one request.json. Use for ANY Blueprint structure/graph/node/pin/edit/create task; do not hand-parse .uasset.
globs:
alwaysApply: false
---
Use the project's Blueprint Agent for Blueprint understand/edit/create.

Entry: `powershell -NoProfile -File "{{ENTRY}}" -RequestJson "<request.json>"`

1. First call `task_type:"status"` (read-only) -> read `capability_state.json` (stage, capabilities, warmup_required).
2. If `understand_full` true -> analyze/edit/create (`execution.mode:"auto"`). If `warmup_required` and user allows a build -> `task_type:"warmup"` (engine_policy.allow_incremental_compile=true), then proceed. Else python_partial/offline (PARTIAL; never call it full).
3. Read results from `manifest.json` / `dispatch_manifest.json`.

Schemas: `{{DOCS}}/request_schemas.md`. Descriptor: `Tools/BlueprintAgent/blueprint_agent.manifest.json`. Examples: `Tools/BlueprintAgent/requests/`.
Safety: status/analyze read-only; warmup builds (consent); edit needs allow_destructive_edit + has rollback; create obeys overwrite_policy.
'@
$cursorDir = Join-Path $TargetDir '.cursor\rules'; New-Item -ItemType Directory -Force -Path $cursorDir | Out-Null
Set-Content -Encoding UTF8 (Join-Path $cursorDir 'blueprint-agent.mdc') $mdc
[void]$written.Add(@{ file='.cursor/rules/blueprint-agent.mdc'; action='written' })

# ---- Claude Code slash command ----
$cmd = Expand @'
---
description: Call the Blueprint Agent to understand / edit / create a UE Blueprint
---
You are driving this project's Blueprint Agent. User request: $ARGUMENTS

Do NOT hand-parse .uasset or guess. Follow this flow:
1. PROBE (read-only): write a request `{ "schema_version":"1.0","task_type":"status","project":{"uproject":"{{UPROJECT}}"} }`
   and run: `powershell -NoProfile -File "{{ENTRY}}" -RequestJson <that file>`. Read `capability_state.json`.
2. DECIDE: if capabilities.understand_full -> proceed. If warmup_required and the user allows building,
   run task_type "warmup" (project.engine_policy.allow_incremental_compile=true). Else use python_partial/offline (PARTIAL).
3. WORK: build the analyze/edit/create request (templates in Tools/BlueprintAgent/requests/, schema in
   {{DOCS}}/request_schemas.md), run it via the entry above, then report by reading manifest.json.
Never present partial results as full; respect safety (read-only status/analyze; consent for warmup; allow_destructive_edit for destructive edits; overwrite_policy for create).
'@
$claudeDir = Join-Path $TargetDir '.claude\commands'; New-Item -ItemType Directory -Force -Path $claudeDir | Out-Null
Set-Content -Encoding UTF8 (Join-Path $claudeDir 'blueprint.md') $cmd
[void]$written.Add(@{ file='.claude/commands/blueprint.md'; action='written' })

# ---- machine-readable descriptor ----
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
$descriptor = [ordered]@{
  schema_version='1.0'; name='blueprint-agent'; version='1.0'
  purpose='Analyze/understand, edit, and create Unreal Engine Blueprints & Widget Blueprints via one request.json.'
  entry=[ordered]@{ interpreter='powershell'; script=$entry; arg='-RequestJson <request.json>' }
  first_call='status'
  task_types=@('status','warmup','analyze','edit','create','validate')
  modes=@('auto','native_full','python_partial','offline_asset_scan')
  request_schema="$docsPath/request_schemas.md"
  docs=[ordered]@{ call="$docsPath/agent_call_contract.md"; edit="$docsPath/agent_edit_contract.md"; create="$docsPath/agent_create_contract.md"; engine="$docsPath/engine_compatibility.md"; warmup="$docsPath/warmup_and_capability_state.md"; fallback="$docsPath/fallback_modes.md" }
  outputs=[ordered]@{ primary='manifest.json'; dispatch='dispatch_manifest.json'; state='capability_state.json'; ir='blueprint_ir.json | partial_ir.json | created_ir.json' }
  status_values=@('success','partial','failed','rolled_back','exists_refused','bad_input')
  plugin_source=$pluginSrc
  safety='status/analyze read-only; warmup builds (consent); edit needs allow_destructive_edit + rollback; create obeys overwrite_policy; blueprint assets never modified without an explicit edit/create task.'
}
($descriptor | ConvertTo-Json -Depth 8) | Set-Content -Encoding UTF8 (Join-Path $toolsDir 'blueprint_agent.manifest.json')
[void]$written.Add(@{ file='Tools/BlueprintAgent/blueprint_agent.manifest.json'; action='written' })

# ---- example requests (with the resolved uproject) ----
$reqDir = Join-Path $toolsDir 'requests'; New-Item -ItemType Directory -Force -Path $reqDir | Out-Null
$mk = { param($obj,$name) ($obj | ConvertTo-Json -Depth 20) | Set-Content -Encoding UTF8 (Join-Path $reqDir $name) }
& $mk (@{ schema_version='1.0'; task_type='status'; project=@{ uproject=$uprojForDoc } }) 'status.json'
& $mk (@{ schema_version='1.0'; task_type='warmup'; project=@{ uproject=$uprojForDoc; engine_policy=@{ allow_incremental_compile=$true } }; request=@{ smoke_asset_path='/Game/...' } }) 'warmup.json'
& $mk (@{ schema_version='1.0'; task_type='analyze'; project=@{ uproject=$uprojForDoc }; execution=@{ mode='auto' }; request=@{ asset_paths=@('/Game/UI/WBP_Example') } }) 'analyze.json'
[void]$written.Add(@{ file='Tools/BlueprintAgent/requests/{status,warmup,analyze}.json'; action='written' })

# ---- install summary ----
$summary = [ordered]@{ schema_version='1.0'; installed_at=(Get-Date).ToUniversalTime().ToString('o');
  target_dir=$TargetDir; agent_root=$AgentRoot; mode=$(if($Reference){'reference'}else{'copy'});
  entry=$entry; uproject=$ProjectUProject; files=@($written) }
($summary | ConvertTo-Json -Depth 8) | Set-Content -Encoding UTF8 (Join-Path $toolsDir 'onboarding_install.json')

Write-Host "== Blueprint Agent onboarding installed ==" -ForegroundColor Green
$written | ForEach-Object { Write-Host ("  {0,-10} {1}" -f $_.action,$_.file) }
Write-Host "entry: $entry"
Write-Host "Other-project AIs (Claude Code/Codex/Cursor) will now auto-discover the agent via AGENTS.md / CLAUDE.md / .cursor / .claude."
exit 0
