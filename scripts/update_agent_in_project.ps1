<#
  update_agent_in_project.ps1 - idempotent, non-destructive update of an installed Blueprint Agent in a
  target project from a source agent repo. Backs up the installed agent, refreshes ONLY managed content,
  preserves user files + non-managed doc content, flags re-warmup when plugin source changed, and never
  touches blueprint assets. Never hot-replaces a loaded plugin DLL while the editor is open.

  Usage:
    .\update_agent_in_project.ps1 -TargetDir "D:\Projects\AClient" -SourceAgentRoot "D:\Projects\BlueprintAgent" `
       [-ProjectUProject "..."] [-Mode copy|reference] [-RunWarmupAfterUpdate] [-AllowUProjectEdit] `
       [-AllowOverwriteManagedFiles] [-DryRun] [-Strict]

  Exit codes: 0 success, 10 partial, 20 failed, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $TargetDir,
  [string] $SourceAgentRoot = "",
  [string] $ProjectUProject = "",
  [ValidateSet('','copy','reference')] [string] $Mode = "",
  [switch] $RunWarmupAfterUpdate,
  [switch] $AllowUProjectEdit,
  [switch] $AllowOverwriteManagedFiles,
  [switch] $DryRun,
  [switch] $Strict
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'agent_sync_lib.ps1')
function Info($m){ Write-Host "[update] $m" -ForegroundColor Cyan }
function Warn($m){ Write-Host "[update] $m" -ForegroundColor Yellow }

if (-not (Test-Path $TargetDir)) { Write-Error "TargetDir not found: $TargetDir"; exit 30 }
$sync = Read-SyncState $TargetDir
if (-not $SourceAgentRoot) { if ($sync -and $sync.source_agent_root) { $SourceAgentRoot = "$($sync.source_agent_root)" } }
if (-not $SourceAgentRoot) { $SourceAgentRoot = Split-Path $PSScriptRoot -Parent }
if (-not (Test-Path (Join-Path $SourceAgentRoot 'scripts\blueprint_agent.ps1'))) { Write-Error "SourceAgentRoot invalid (no scripts/blueprint_agent.ps1): $SourceAgentRoot"; exit 30 }
$srcVer = Read-AgentVersion $SourceAgentRoot
if (-not $srcVer) { Write-Error "Source has no blueprint_agent.version.json: $SourceAgentRoot"; exit 30 }

if (-not $Mode) { $Mode = if ($sync -and $sync.install_mode) { "$($sync.install_mode)" } else { 'copy' } }
if (-not $ProjectUProject) {
  if ($sync -and $sync.project_uproject) { $ProjectUProject = "$($sync.project_uproject)" }
  else { $up = Get-ChildItem $TargetDir -Filter *.uproject -EA SilentlyContinue | Select-Object -First 1; if ($up) { $ProjectUProject = $up.FullName } }
}
$uprojForDoc = if ($ProjectUProject) { $ProjectUProject -replace '\\','/' } else { "<PATH>/YourProject.uproject" }

$toolsDir = Join-Path $TargetDir 'Tools\BlueprintAgent'
$ts = Get-Date -Format 'yyyyMMdd_HHmmss'
$updDir = Join-Path $TargetDir "Saved\BPParserAgentReports\update\$ts"
New-Item -ItemType Directory -Force -Path $updDir | Out-Null

# ---- pre-checks ----
$installed = [bool]$sync
$editorRunning = Test-EditorRunning
$pluginChanged = if ($installed) { Test-PluginChanged $SourceAgentRoot $sync } else { $true }
$conflicts = if ($installed) { @(Get-ManagedConflicts $TargetDir $sync) } else { @() }
$conflictCount = @($conflicts).Count

$prevVer = if ($sync) { [ordered]@{ agent_version="$($sync.installed_agent_version)"; agent_commit="$($sync.installed_agent_commit)" } } else { [ordered]@{ agent_version=''; agent_commit='' } }
$newVer  = [ordered]@{ agent_version="$($srcVer.agent_version)"; agent_commit="$($srcVer.agent_commit)" }

# ---- update_plan.json ----
$actions = New-Object System.Collections.ArrayList
[void]$actions.Add(@{ action='backup_current_agent'; required=$true })
if ($Mode -eq 'copy') { [void]$actions.Add(@{ action='update_tools_blueprint_agent'; required=$true }) }
else { [void]$actions.Add(@{ action='refresh_reference_pointers'; required=$true }) }
[void]$actions.Add(@{ action='update_managed_blocks'; required=$true })
if ($pluginChanged) { [void]$actions.Add(@{ action='mark_warmup_required'; required=$true; reason='plugin_source_changed' }) }

$plan = [ordered]@{
  schema_version='1.0'; target_project=($TargetDir -replace '\\','/'); project_uproject=$uprojForDoc
  source_agent_root=($SourceAgentRoot -replace '\\','/'); install_mode=$Mode
  current_version=$prevVer; source_version=$newVer
  changes=[ordered]@{ plugin_source=$(if($pluginChanged){'changed'}else{'unchanged'}); managed_files_modified_in_target=$conflictCount }
  actions=@($actions)
  requires_rebuild=$pluginChanged; requires_warmup=$pluginChanged; requires_editor_restart=$false
  editor_running=$editorRunning
  risk_notes=@( if($editorRunning -and $pluginChanged){'Editor is open; plugin DLL cannot be hot-replaced. Close editor before re-warmup.'} )
}
Write-Utf8NoBom (Join-Path $updDir 'update_plan.json') ($plan | ConvertTo-Json -Depth 10)
Info "plan -> $(Join-Path $updDir 'update_plan.json')  (mode=$Mode plugin_changed=$pluginChanged conflicts=$conflictCount editor_running=$editorRunning)"

$updated = New-Object System.Collections.ArrayList
$skipped = New-Object System.Collections.ArrayList
$backups = New-Object System.Collections.ArrayList
$errors  = New-Object System.Collections.ArrayList
$warnings= New-Object System.Collections.ArrayList

if ($DryRun) {
  $result = [ordered]@{ schema_version='1.0'; status='planned'; dry_run=$true; target_project=($TargetDir -replace '\\','/')
    previous_version=$prevVer; new_version=$newVer; install_mode=$Mode
    plugin_source_changed=$pluginChanged; conflicts=@($conflicts); editor_running=$editorRunning
    requires_warmup=$pluginChanged; requires_rebuild=$pluginChanged; requires_editor_restart=$false
    next_actions=@( if($pluginChanged){'Plugin changed: re-warmup after update.'} else {'No rebuild needed.'} )
    plan=(Join-Path $updDir 'update_plan.json') }
  Write-Utf8NoBom (Join-Path $updDir 'update_result.json') ($result | ConvertTo-Json -Depth 10)
  Info "DRY-RUN: no changes made. result -> $(Join-Path $updDir 'update_result.json')"
  $result | Write-Output
  exit 0
}

# ---- 1. backup current installed agent ----
if ($installed -and (Test-Path $toolsDir)) {
  $backupDir = Join-Path $updDir 'backup\Tools_BlueprintAgent'
  New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
  robocopy $toolsDir $backupDir /E /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { [void]$errors.Add("backup failed (robocopy exit $LASTEXITCODE)") } else { [void]$backups.Add(($backupDir -replace '\\','/')) }
} else { [void]$warnings.Add("no prior install detected; performing a fresh managed-content write") }

# also back up managed-block host files that we will touch
foreach($mb in @('AGENTS.md','CLAUDE.md','GEMINI.md')){
  $f = Join-Path $TargetDir $mb
  if (Test-Path $f) { $b = Join-Path $updDir "backup\$mb"; Copy-Item $f $b -Force; [void]$backups.Add(($b -replace '\\','/')) }
}

# ---- resolve managed paths for this mode ----
if ($Mode -eq 'reference') {
  $entry='' ; $entry = Join-Path $SourceAgentRoot 'scripts\blueprint_agent.ps1'
  $docsPath = Join-Path $SourceAgentRoot 'docs'
  $pluginSrc = Join-Path $SourceAgentRoot 'bpparser_testgen\Plugins\BPParserTestGen'
} else {
  $entry = 'Tools\BlueprintAgent\scripts\blueprint_agent.ps1'
  $docsPath = 'Tools/BlueprintAgent/docs'
  $pluginSrc = 'Tools\BlueprintAgent\plugin\BPParserTestGen'
}

# ---- 2. refresh managed tree (copy mode only) ----
if ($Mode -eq 'copy') {
  if ($editorRunning -and $pluginChanged) {
    [void]$warnings.Add("editor running: refreshing scripts/docs, but plugin DLL cannot be hot-replaced; re-warmup after closing the editor")
  }
  if (Copy-ManagedTree (Join-Path $SourceAgentRoot 'scripts') (Join-Path $toolsDir 'scripts')) { [void]$updated.Add('Tools/BlueprintAgent/scripts') } else { [void]$errors.Add('failed to refresh scripts') }
  if (Copy-ManagedTree (Join-Path $SourceAgentRoot 'docs')    (Join-Path $toolsDir 'docs'))    { [void]$updated.Add('Tools/BlueprintAgent/docs') }    else { [void]$errors.Add('failed to refresh docs') }
  # plugin source: refresh the Tools copy (the built copy under Plugins/ is refreshed by warmup, not here)
  if (Copy-ManagedTree (Join-Path $SourceAgentRoot 'bpparser_testgen\Plugins\BPParserTestGen') (Join-Path $toolsDir 'plugin\BPParserTestGen')) { [void]$updated.Add('Tools/BlueprintAgent/plugin/BPParserTestGen') } else { [void]$errors.Add('failed to refresh plugin source') }
} else {
  [void]$updated.Add('reference-mode: pointers refreshed (no copy)')
}

# ---- 3. managed blocks (AGENTS/CLAUDE always; GEMINI only if present) ----
$block = Get-BABlock $entry $docsPath $uprojForDoc $pluginSrc
foreach($mb in @('AGENTS.md','CLAUDE.md')){ $a = Upsert-ManagedBlock (Join-Path $TargetDir $mb) $block; [void]$updated.Add("$mb (block:$a)") }
$gem = Join-Path $TargetDir 'GEMINI.md'; if (Test-Path $gem) { $a = Upsert-ManagedBlock $gem $block; [void]$updated.Add("GEMINI.md (block:$a)") }

# ---- 4. Cursor rule + Claude command ----
$cursorDir = Join-Path $TargetDir '.cursor\rules'; New-Item -ItemType Directory -Force -Path $cursorDir | Out-Null
Write-Utf8NoBom (Join-Path $cursorDir 'blueprint-agent.mdc') (Get-BAMdc $entry $docsPath $uprojForDoc $pluginSrc); [void]$updated.Add('.cursor/rules/blueprint-agent.mdc')
$claudeDir = Join-Path $TargetDir '.claude\commands'; New-Item -ItemType Directory -Force -Path $claudeDir | Out-Null
Write-Utf8NoBom (Join-Path $claudeDir 'blueprint.md') (Get-BACommand $entry $docsPath $uprojForDoc $pluginSrc); [void]$updated.Add('.claude/commands/blueprint.md')

# ---- 5. descriptor + version + request templates ----
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
$warmupRequired = [bool]$pluginChanged
$descriptor = Get-BADescriptor $entry $docsPath $pluginSrc $srcVer
$descriptor.install = [ordered]@{
  installed_agent_version="$($srcVer.agent_version)"; installed_agent_commit="$($srcVer.agent_commit)"
  install_mode=$Mode; source_agent_root=($SourceAgentRoot -replace '\\','/')
  last_update_time=(Get-Date).ToUniversalTime().ToString('o'); last_update_status='success'
  warmup_required_after_update=$warmupRequired
}
Write-Utf8NoBom (Join-Path $toolsDir 'blueprint_agent.manifest.json') ($descriptor | ConvertTo-Json -Depth 10); [void]$updated.Add('Tools/BlueprintAgent/blueprint_agent.manifest.json')
Write-Utf8NoBom (Join-Path $toolsDir 'blueprint_agent.version.json') ($srcVer | ConvertTo-Json -Depth 8); [void]$updated.Add('Tools/BlueprintAgent/blueprint_agent.version.json')

$reqDir = Join-Path $toolsDir 'requests'; New-Item -ItemType Directory -Force -Path $reqDir | Out-Null
$mk = { param($obj,$name) Write-Utf8NoBom (Join-Path $reqDir $name) ($obj | ConvertTo-Json -Depth 20) }
& $mk (@{ schema_version='1.0'; task_type='status'; project=@{ uproject=$uprojForDoc } }) 'status.template.json'
& $mk (@{ schema_version='1.0'; task_type='warmup'; project=@{ uproject=$uprojForDoc; engine_policy=@{ allow_incremental_compile=$true } }; request=@{ smoke_asset_path='/Game/...' } }) 'warmup.template.json'
& $mk (@{ schema_version='1.0'; task_type='analyze'; project=@{ uproject=$uprojForDoc }; execution=@{ mode='auto' }; request=@{ asset_paths=@('/Game/UI/WBP_Example') } }) 'analyze.template.json'
& $mk (@{ schema_version='1.0'; task_type='update'; project=@{ uproject=$uprojForDoc }; request=@{ source_agent_root=($SourceAgentRoot -replace '\\','/') } }) 'update.template.json'
[void]$updated.Add('Tools/BlueprintAgent/requests/*.template.json')
# user-authored (non-template) request files are preserved untouched
Get-ChildItem $reqDir -Filter *.json -EA SilentlyContinue | Where-Object { $_.Name -notlike '*.template.json' } | ForEach-Object { [void]$skipped.Add("Tools/BlueprintAgent/requests/$($_.Name) (user file preserved)") }

# ---- 6. warmup marking (capability_state) when plugin changed ----
$statusDir = Join-Path $TargetDir 'Saved\BPParserAgentReports\status'
if ($pluginChanged) {
  New-Item -ItemType Directory -Force -Path $statusDir | Out-Null
  $cap = [ordered]@{ schema_version='1.0'; stage='needs_warmup_after_update'; plugin_installed=$true; plugin_built=$false
    warmup_required=$true; reason='plugin_source_changed_since_last_build'; generated_at=(Get-Date).ToUniversalTime().ToString('o') }
  Write-Utf8NoBom (Join-Path $statusDir 'capability_state.json') ($cap | ConvertTo-Json -Depth 6)
  [void]$updated.Add('Saved/BPParserAgentReports/status/capability_state.json (needs_warmup_after_update)')
  [void]$warnings.Add("plugin source changed: native_full NOT ready until re-warmup succeeds")
}

# ---- 7. sync state (record new version + fresh hashes) ----
$hashes = Get-TargetManagedHashes $TargetDir
$syncOut = [ordered]@{
  schema_version='1.0'
  installed_agent_version="$($srcVer.agent_version)"; installed_agent_commit="$($srcVer.agent_commit)"
  install_mode=$Mode; source_agent_root=($SourceAgentRoot -replace '\\','/'); project_uproject=$uprojForDoc
  installed_at=$(if($sync -and $sync.installed_at){"$($sync.installed_at)"}else{(Get-Date).ToUniversalTime().ToString('o')})
  last_update_time=(Get-Date).ToUniversalTime().ToString('o'); last_update_status='success'
  warmup_required_after_update=$warmupRequired
  managed_hashes=$hashes
}
Write-Utf8NoBom (Get-SyncStatePath $TargetDir) ($syncOut | ConvertTo-Json -Depth 8)

# ---- 8. optional re-warmup (guarded) ----
$warmupRan = $false
if ($RunWarmupAfterUpdate -and $pluginChanged) {
  if ($editorRunning) { [void]$warnings.Add("RunWarmupAfterUpdate requested but editor is running; skipping (close editor first)") }
  else {
    Info "running warmup (plugin changed)..."
    $wargs = @{ ProjectUProject=$ProjectUProject }
    & (Join-Path $PSScriptRoot 'warmup_project.ps1') @wargs *>&1 | Out-File -Encoding utf8 (Join-Path $updDir 'warmup_run.txt')
    $warmupRan = ($LASTEXITCODE -eq 0)
    if ($warmupRan) {
      Write-Utf8NoBom (Join-Path $statusDir 'capability_state.json') ((@{ schema_version='1.0'; stage='native_ready'; plugin_installed=$true; plugin_built=$true; warmup_required=$false; generated_at=(Get-Date).ToUniversalTime().ToString('o') }) | ConvertTo-Json -Depth 6)
      $warmupRequired=$false
    } else { [void]$errors.Add("warmup failed (see warmup_run.txt)") }
  }
}

# ---- 9. result ----
$status = if ($errors.Count -gt 0) { if ($updated.Count -gt 0) {'partial'} else {'failed'} } else { 'success' }
$next = New-Object System.Collections.ArrayList
if ($pluginChanged -and -not $warmupRan) { [void]$next.Add('Run warmup (plugin source changed) before using native_full. Close the UE editor first if open.') }
if ($conflictCount -gt 0) { [void]$next.Add("$conflictCount managed file(s) were modified in target; originals are in the backup dir.") }
if ($editorRunning -and $pluginChanged) { [void]$next.Add('Editor is open; DLL was not replaced. If using editor_live, plugin_reload_required=true (restart editor after warmup).') }

$result = [ordered]@{
  schema_version='1.0'; status=$status; target_project=($TargetDir -replace '\\','/')
  previous_version=$prevVer; new_version=$newVer; install_mode=$Mode
  updated_files=@($updated); skipped_files=@($skipped); conflicts=@($conflicts); backups=@($backups)
  plugin_source_changed=$pluginChanged
  requires_warmup=$warmupRequired; requires_rebuild=$pluginChanged; requires_editor_restart=$false
  warmup_ran=$warmupRan
  editor_live=[ordered]@{ service_running=$editorRunning; agent_files_updated=$true; plugin_reload_required=[bool]($editorRunning -and $pluginChanged) }
  next_actions=@($next); errors=@($errors); warnings=@($warnings)
  plan=(Join-Path $updDir 'update_plan.json'); generated_at=(Get-Date).ToUniversalTime().ToString('o')
}
Write-Utf8NoBom (Join-Path $updDir 'update_result.json') ($result | ConvertTo-Json -Depth 10)

$col = if ($status -eq 'success') {'Green'} elseif ($status -eq 'partial') {'Yellow'} else {'Red'}
Write-Host ("update_agent_in_project: {0}  {1} -> {2}  plugin_changed={3} warmup_required={4}" -f `
  $status,$prevVer.agent_version,$newVer.agent_version,$pluginChanged,$warmupRequired) -ForegroundColor $col
Write-Host ("result: {0}" -f (Join-Path $updDir 'update_result.json'))
$result | Write-Output
switch ($status) { 'success' { exit 0 } 'partial' { if($Strict){exit 10}else{exit 0} } default { exit 20 } }
