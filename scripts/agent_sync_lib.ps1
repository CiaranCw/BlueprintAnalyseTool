<#
  agent_sync_lib.ps1 - shared helpers for installing / updating the Blueprint Agent into a target project.

  Dot-sourced by install_agent_into_project.ps1, update_agent_in_project.ps1, and
  check_project_agent_version.ps1 so the managed content (AGENTS/CLAUDE block, Cursor rule, Claude command,
  descriptor) and the version/hash logic have ONE source of truth (prevents version drift between install
  and update). No side effects on dot-source.
#>

# Write UTF-8 WITHOUT BOM (clean for every downstream tool/AI).
function Write-Utf8NoBom([string]$path,[string]$text){
  $dir = Split-Path $path -Parent; if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  [IO.File]::WriteAllText($path, [string]$text, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-Sha256Hex([string]$path){
  if (-not (Test-Path $path -PathType Leaf)) { return "" }
  return (Get-FileHash -Path $path -Algorithm SHA256).Hash.ToLower()
}

# Resolve the short git commit of a repo root (empty if not a git repo / git unavailable).
function Resolve-GitShortCommit([string]$root){
  try {
    $c = & git -C $root rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $c) { return "$c".Trim() }
  } catch {}
  return ""
}

# Read the source/target version file; overlay the live git commit when the file leaves it blank.
function Read-AgentVersion([string]$root){
  $vf = Join-Path $root 'blueprint_agent.version.json'
  if (-not (Test-Path $vf)) { return $null }
  try { $v = Get-Content $vf -Raw | ConvertFrom-Json } catch { return $null }
  if (-not $v.agent_commit) {
    $c = Resolve-GitShortCommit $root
    if ($c) { $v | Add-Member -NotePropertyName agent_commit -NotePropertyValue $c -Force }
  }
  return $v
}

# Managed-block markers (must match what onboarding wrote historically).
function Get-ManagedBlockBegin { '<!-- BEGIN BLUEPRINT-AGENT' }
function Get-ManagedBlockEnd   { '<!-- END BLUEPRINT-AGENT (managed) -->' }

# Replace the managed block in $file (create/append if absent). Returns 'replaced'|'repaired'|'appended'|'created'.
function Upsert-ManagedBlock([string]$file,[string]$block){
  $begin = Get-ManagedBlockBegin; $end = Get-ManagedBlockEnd
  if (Test-Path $file) {
    $txt = Get-Content $file -Raw
    $bi = $txt.IndexOf($begin)
    if ($bi -ge 0) {
      $ei = $txt.IndexOf($end)
      if ($ei -ge 0) {
        # well-formed block -> replace exactly between the markers
        $ei += $end.Length
        Write-Utf8NoBom $file ($txt.Substring(0,$bi) + $block + $txt.Substring($ei)); return 'replaced'
      }
      # BEGIN present but END missing (user broke the block): treat everything from BEGIN to EOF as the
      # broken managed region and replace it, so we never leave/duplicate a partial block.
      Write-Utf8NoBom $file ($txt.Substring(0,$bi).TrimEnd() + "`r`n`r`n" + $block + "`r`n"); return 'repaired'
    }
    Write-Utf8NoBom $file ($txt.TrimEnd() + "`r`n`r`n" + $block + "`r`n"); return 'appended'
  } else {
    $hdr = "# " + [IO.Path]::GetFileNameWithoutExtension($file) + "`r`n`r`n"
    Write-Utf8NoBom $file ($hdr + $block + "`r`n"); return 'created'
  }
}

# Extract only the managed-block substring from a file (or "" if none) - used for conflict detection.
function Get-ManagedBlockContent([string]$file){
  if (-not (Test-Path $file)) { return "" }
  $txt = Get-Content $file -Raw
  $begin = Get-ManagedBlockBegin; $end = Get-ManagedBlockEnd
  $bi = $txt.IndexOf($begin); if ($bi -lt 0) { return "" }
  $ei = $txt.IndexOf($end); if ($ei -lt 0) { return "" }
  return $txt.Substring($bi, ($ei + $end.Length) - $bi)
}

# ------------------------------------------------------------------ managed content builders
function Expand-BATemplate([string]$t,[string]$entry,[string]$docs,[string]$uproject,[string]$pluginSrc){
  return $t.Replace('{{ENTRY}}',$entry).Replace('{{DOCS}}',$docs).Replace('{{UPROJECT}}',$uproject).Replace('{{PLUGIN_SRC}}',$pluginSrc)
}

function Get-BABlock([string]$entry,[string]$docs,[string]$uproject,[string]$pluginSrc){
  $t = @'
<!-- BEGIN BLUEPRINT-AGENT (managed by install/update_agent_in_project.ps1; do not edit between markers) -->
## Blueprint Agent - understand / edit / create UE Blueprints (AI-callable)

This project ships a **Blueprint Agent**: a tool to analyze, modify, and create Unreal Engine Blueprints
and Widget Blueprints. When you (an AI) are asked to understand a Blueprint's structure (graphs, nodes,
pins, connections, variables, functions), to modify one, or to create one, USE THIS AGENT - do not
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
`stage` (offline_only|python_only|needs_install|needs_build|native_ready|needs_warmup_after_update),
`capabilities{understand_full,edit,create}`, `warmup_required`, `recommended_action`, `next_calls`.

### Decide
- `capabilities.understand_full == true` -> go straight to analyze/edit/create with `execution.mode:"auto"`.
- `warmup_required == true` AND the user permits a one-time build -> run `task_type:"warmup"` with
  `project.engine_policy.allow_incremental_compile:true` (installs a read-only plugin + incrementally
  builds the Editor against this project's engine; it NEVER modifies assets). Then proceed.
- Otherwise use `execution.mode:"python_partial"` or `"offline"` for PARTIAL understanding (no full
  graph). Never present a partial result as full.

### Prefer an already-open editor (editor_live)
If the project's UE editor is open, add `-PreferEditorLive` (or `execution.mode:"editor_live"`) so requests
run in the open editor with NO cold start; on unavailable it falls back to native_full. See
`{{DOCS}}/editor_live_mode.md`.

### Task types
`status | warmup | analyze | edit | create | validate | update`. Request shapes and node/pin/edge schema:
see `{{DOCS}}/request_schemas.md`; machine descriptor: `Tools/BlueprintAgent/blueprint_agent.manifest.json`;
runnable examples: `Tools/BlueprintAgent/requests/`.

### Keeping up to date (installed but maybe not latest)
This is an INSTALLED copy of the agent; the source repo may have moved on. Before relying on it, run
`Tools/BlueprintAgent/scripts/check_project_agent_version.ps1 -TargetDir "<project>" -SourceAgentRoot "<agent repo>"`
(reads `blueprint_agent.version.json` both sides). If outdated, run `update_agent_in_project.ps1` (or
`task_type:"update"` with `request.source_agent_root`). If the plugin source changed, status becomes
`needs_warmup_after_update` - do NOT claim native_full is ready until warmup succeeds again.

### Read results
Everything is in `manifest.json` / `dispatch_manifest.json`: `status` (success|partial|failed|
rolled_back|exists_refused), `mode`, `outputs`, `warnings`, `errors`, `manual_check_required`, `next_actions`.

### Safety
`status`/`analyze` are read-only. `warmup` builds (needs consent). `edit` needs `allow_destructive_edit`
for destructive ops and always keeps a backup/rollback. `create` obeys `overwrite_policy`
(fail_if_exists|create_unique_name|overwrite_if_allowed) and never overwrites by default. `update` backs up
the installed agent, updates managed files only, and never touches blueprint assets or `.uproject` (unless
explicitly allowed).
<!-- END BLUEPRINT-AGENT (managed) -->
'@
  return Expand-BATemplate $t $entry $docs $uproject $pluginSrc
}

function Get-BAMdc([string]$entry,[string]$docs,[string]$uproject,[string]$pluginSrc){
  $t = @'
---
description: UE Blueprint Agent - analyze/understand, edit, or create Unreal Blueprints & Widget Blueprints via one request.json. Use for ANY Blueprint structure/graph/node/pin/edit/create task; do not hand-parse .uasset.
globs:
alwaysApply: false
---
Use the project's Blueprint Agent for Blueprint understand/edit/create.

Entry: `powershell -NoProfile -File "{{ENTRY}}" -RequestJson "<request.json>"`

1. First call `task_type:"status"` (read-only) -> read `capability_state.json` (stage, capabilities, warmup_required).
2. If `understand_full` true -> analyze/edit/create (`execution.mode:"auto"`; add `-PreferEditorLive` when the editor is open). If `warmup_required` and user allows a build -> `task_type:"warmup"` (engine_policy.allow_incremental_compile=true), then proceed. Else python_partial/offline (PARTIAL; never call it full).
3. Read results from `manifest.json` / `dispatch_manifest.json`.

Stay current: this is an installed copy. Run `check_project_agent_version.ps1`; if outdated run `update_agent_in_project.ps1` (or `task_type:"update"`). If plugin source changed -> `needs_warmup_after_update` (re-warmup before claiming native_full).

Schemas: `{{DOCS}}/request_schemas.md`. Descriptor: `Tools/BlueprintAgent/blueprint_agent.manifest.json`. Examples: `Tools/BlueprintAgent/requests/`.
Safety: status/analyze read-only; warmup builds (consent); edit needs allow_destructive_edit + has rollback; create obeys overwrite_policy.
'@
  return Expand-BATemplate $t $entry $docs $uproject $pluginSrc
}

function Get-BACommand([string]$entry,[string]$docs,[string]$uproject,[string]$pluginSrc){
  $t = @'
---
description: Call the Blueprint Agent to understand / edit / create a UE Blueprint
---
You are driving this project's Blueprint Agent. User request: $ARGUMENTS

Do NOT hand-parse .uasset or guess. Follow this flow:
1. PROBE (read-only): write a request `{ "schema_version":"1.0","task_type":"status","project":{"uproject":"{{UPROJECT}}"} }`
   and run: `powershell -NoProfile -File "{{ENTRY}}" -RequestJson <that file>`. Read `capability_state.json`.
2. DECIDE: if capabilities.understand_full -> proceed. If warmup_required and the user allows building,
   run task_type "warmup" (project.engine_policy.allow_incremental_compile=true). Else use python_partial/offline (PARTIAL).
   If the editor is already open, add -PreferEditorLive to reuse it (editor_live) with no cold start.
3. WORK: build the analyze/edit/create request (templates in Tools/BlueprintAgent/requests/, schema in
   {{DOCS}}/request_schemas.md), run it via the entry above, then report by reading manifest.json.
Stay current: if unsure the agent is latest, run check_project_agent_version.ps1; update via update_agent_in_project.ps1 or task_type "update".
Never present partial results as full; respect safety (read-only status/analyze; consent for warmup; allow_destructive_edit for destructive edits; overwrite_policy for create).
'@
  return Expand-BATemplate $t $entry $docs $uproject $pluginSrc
}

# Descriptor (blueprint_agent.manifest.json). $ver is the source Read-AgentVersion object (may be $null).
function Get-BADescriptor([string]$entry,[string]$docsPath,[string]$pluginSrc,$ver){
  $d = [ordered]@{
    schema_version='1.0'; name='blueprint-agent'
    version=$(if($ver){"$($ver.agent_version)"}else{'1.0'})
    agent_commit=$(if($ver){"$($ver.agent_commit)"}else{''})
    purpose='Analyze/understand, edit, and create Unreal Engine Blueprints & Widget Blueprints via one request.json.'
    entry=[ordered]@{ interpreter='powershell'; script=$entry; arg='-RequestJson <request.json>' }
    first_call='status'
    task_types=@('status','warmup','analyze','edit','create','validate','update')
    modes=@('auto','editor_live','native_full','python_partial','offline_asset_scan')
    request_schema="$docsPath/request_schemas.md"
    docs=[ordered]@{ call="$docsPath/agent_call_contract.md"; edit="$docsPath/agent_edit_contract.md"; create="$docsPath/agent_create_contract.md"; engine="$docsPath/engine_compatibility.md"; warmup="$docsPath/warmup_and_capability_state.md"; fallback="$docsPath/fallback_modes.md"; editor_live="$docsPath/editor_live_mode.md"; update="$docsPath/update_sync_protocol.md" }
    outputs=[ordered]@{ primary='manifest.json'; dispatch='dispatch_manifest.json'; state='capability_state.json'; ir='blueprint_ir.json | partial_ir.json | created_ir.json' }
    status_values=@('success','partial','failed','rolled_back','exists_refused','bad_input')
    plugin_source=$pluginSrc
    safety='status/analyze read-only; warmup builds (consent); edit needs allow_destructive_edit + rollback; create obeys overwrite_policy; blueprint assets never modified without an explicit edit/create task.'
  }
  return $d
}

# ------------------------------------------------------------------ managed tree hashing
# Files whose names should NEVER be copied into a target (driver prompts + build artifacts handled via -XD).
$script:BADriverPromptPatterns = @('*exploration*','*update_sync*')

function Test-BAExcludedName([string]$name){
  foreach($p in $script:BADriverPromptPatterns){ if ($name -like $p) { return $true } }
  return $false
}

# Hash every file under $root (relative-path keyed), skipping excluded dirs/names. Returns an ordered hashtable.
function Get-ManagedTreeHashes([string]$root,[string[]]$excludeDirs=@('Binaries','Intermediate','.git')){
  $out = [ordered]@{}
  if (-not (Test-Path $root)) { return $out }
  $rootFull = (Resolve-Path $root).Path.TrimEnd('\','/')
  Get-ChildItem -Path $root -Recurse -File -Force -EA SilentlyContinue | ForEach-Object {
    $full = $_.FullName
    $skip = $false
    foreach($d in $excludeDirs){ if ($full -match [regex]::Escape("\$d\")) { $skip=$true; break } }
    if ($skip) { return }
    if (Test-BAExcludedName $_.Name) { return }
    $rel = $full.Substring($rootFull.Length).TrimStart('\','/') -replace '\\','/'
    $out[$rel] = (Get-FileHash -Path $full -Algorithm SHA256).Hash.ToLower()
  }
  return $out
}

# Copy a managed tree (mirror) excluding build artifacts + driver prompts. Returns robocopy-agnostic bool.
function Copy-ManagedTree([string]$src,[string]$dst){
  if (-not (Test-Path $src)) { return $false }
  New-Item -ItemType Directory -Force -Path $dst | Out-Null
  $xf = @(); foreach($p in $script:BADriverPromptPatterns){ $xf += $p }
  # /MIR mirrors (so removed source files disappear); exclude Binaries/Intermediate/.git + driver prompts.
  robocopy $src $dst /MIR /XD Binaries Intermediate .git /XF @xf /NFL /NDL /NJH /NJS /NP | Out-Null
  # robocopy exit codes 0-7 are success; >=8 is failure
  return ($LASTEXITCODE -lt 8)
}

# SHA-256 of a managed-block's content (or "" if the file has no block) - for block conflict detection.
function Get-ManagedBlockHash([string]$file){
  $c = Get-ManagedBlockContent $file
  if (-not $c) { return "" }
  $bytes = [Text.Encoding]::UTF8.GetBytes($c)
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return (-join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })) } finally { $sha.Dispose() }
}

# The canonical set of managed-file hashes in a target project (tools tree + dotfiles + managed blocks).
# Used at install time (to record) and at check/update time (to detect user edits). Ordered, relpath-keyed.
function Get-TargetManagedHashes([string]$TargetDir){
  $out = [ordered]@{}
  $tools = Join-Path $TargetDir 'Tools\BlueprintAgent'
  if (Test-Path $tools) {
    $h = Get-ManagedTreeHashes $tools
    foreach($k in $h.Keys){
      if ($k -like '.agent_sync/*') { continue }         # our own state, not managed content
      if ($k -eq 'onboarding_install.json') { continue }  # install log
      if ($k -like 'requests/*' -and ($k -notlike '*.template.json')) { continue } # user-owned request files
      $out["Tools/BlueprintAgent/$k"] = $h[$k]
    }
  }
  $mdc = Join-Path $TargetDir '.cursor\rules\blueprint-agent.mdc'
  if (Test-Path $mdc) { $out['.cursor/rules/blueprint-agent.mdc'] = (Get-Sha256Hex $mdc) }
  $cmd = Join-Path $TargetDir '.claude\commands\blueprint.md'
  if (Test-Path $cmd) { $out['.claude/commands/blueprint.md'] = (Get-Sha256Hex $cmd) }
  foreach($mb in @('AGENTS.md','CLAUDE.md','GEMINI.md')){
    $f = Join-Path $TargetDir $mb
    $bh = Get-ManagedBlockHash $f
    if ($bh) { $out["$mb#managed_block"] = $bh }
  }
  return $out
}

# Persist / read the sync-state (installed version + install mode + recorded hashes).
function Get-SyncStatePath([string]$TargetDir){ return (Join-Path $TargetDir 'Tools\BlueprintAgent\.agent_sync\sync_state.json') }

function Read-SyncState([string]$TargetDir){
  $p = Get-SyncStatePath $TargetDir
  if (-not (Test-Path $p)) { return $null }
  try { return (Get-Content $p -Raw | ConvertFrom-Json) } catch { return $null }
}

function Get-SourcePluginRoot([string]$SourceAgentRoot){ return (Join-Path $SourceAgentRoot 'bpparser_testgen\Plugins\BPParserTestGen') }

# True if the SOURCE plugin content differs from what was recorded at last install/update (=> needs warmup).
function Test-PluginChanged([string]$SourceAgentRoot,$syncState){
  $srcH = Get-ManagedTreeHashes (Get-SourcePluginRoot $SourceAgentRoot)
  $recorded = @{}
  if ($syncState -and $syncState.managed_hashes){
    $prefix = 'Tools/BlueprintAgent/plugin/BPParserTestGen/'
    foreach($p in $syncState.managed_hashes.PSObject.Properties){
      if ($p.Name -like ($prefix + '*')){ $recorded[$p.Name.Substring($prefix.Length)] = $p.Value }
    }
  }
  if ($recorded.Count -eq 0){ return $true }   # unknown baseline -> assume changed (safe)
  foreach($k in $srcH.Keys){ if (-not $recorded.ContainsKey($k) -or $recorded[$k] -ne $srcH[$k]){ return $true } }
  foreach($k in $recorded.Keys){ if (-not $srcH.Contains($k)){ return $true } }
  return $false
}

# Managed files the user changed since our last write (compares current target hashes to recorded baseline).
# Returns an array of PSCustomObject (NOT ordered dicts: @()/.Count would otherwise enumerate dict keys).
function Get-ManagedConflicts([string]$TargetDir,$syncState){
  $conf = [System.Collections.ArrayList]::new()
  if (-not $syncState -or -not $syncState.managed_hashes){ return @() }
  $cur = Get-TargetManagedHashes $TargetDir
  foreach($p in $syncState.managed_hashes.PSObject.Properties){
    if ($cur.Contains($p.Name) -and ($cur[$p.Name] -ne $p.Value)){
      [void]$conf.Add([pscustomobject]@{ path=$p.Name; type='modified_in_target' })
    }
  }
  return ,($conf.ToArray())
}

# True if the target's UE editor process is running (blocks plugin DLL replacement / warmup).
function Test-EditorRunning{
  return [bool](Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue)
}
