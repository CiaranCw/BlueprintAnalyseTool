<#
  check_project_agent_version.ps1 - report whether a target project's installed Blueprint Agent is current
  vs a source agent repo, and whether an update / re-warmup is needed. READ-ONLY (no files changed except
  the machine-readable check_result.json). Never touches blueprint assets.

  Usage:
    .\check_project_agent_version.ps1 -TargetDir "D:\Projects\AClient" -SourceAgentRoot "D:\Projects\BlueprintAgent" [-ProjectUProject "..."]

  Exit codes: 0 up-to-date, 10 update available, 12 not installed, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $TargetDir,
  [string] $SourceAgentRoot = "",
  [string] $ProjectUProject = ""
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'agent_sync_lib.ps1')

if (-not (Test-Path $TargetDir)) { Write-Error "TargetDir not found: $TargetDir"; exit 30 }
$sync = Read-SyncState $TargetDir
# resolve SourceAgentRoot: explicit > recorded install source > this repo root
if (-not $SourceAgentRoot) { if ($sync -and $sync.source_agent_root) { $SourceAgentRoot = "$($sync.source_agent_root)" } }
if (-not $SourceAgentRoot) { $SourceAgentRoot = Split-Path $PSScriptRoot -Parent }

$srcVer = Read-AgentVersion $SourceAgentRoot
$installed = [bool]$sync
$outDir = Join-Path $TargetDir 'Saved\BPParserAgentReports\update'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$curVer = if ($sync) { [ordered]@{ agent_version="$($sync.installed_agent_version)"; agent_commit="$($sync.installed_agent_commit)" } } else { [ordered]@{ agent_version=''; agent_commit='' } }
$srcVerOut = if ($srcVer) { [ordered]@{ agent_version="$($srcVer.agent_version)"; agent_commit="$($srcVer.agent_commit)" } } else { [ordered]@{ agent_version=''; agent_commit='' } }

$installMode = if ($sync) { "$($sync.install_mode)" } else { 'none' }

# up-to-date? compare commit when both present, else version.
$isUpToDate = $false
if ($installed -and $srcVer) {
  if ($curVer.agent_commit -and $srcVerOut.agent_commit) { $isUpToDate = ($curVer.agent_commit -eq $srcVerOut.agent_commit) }
  else { $isUpToDate = ($curVer.agent_version -eq $srcVerOut.agent_version) -and $curVer.agent_version -ne '' }
}

$pluginChanged = $false
if ($installed -and $srcVer) { $pluginChanged = Test-PluginChanged $SourceAgentRoot $sync }
$conflicts = if ($installed) { @(Get-ManagedConflicts $TargetDir $sync) } else { @() }
$conflictCount = @($conflicts).Count
$editorRunning = Test-EditorRunning

$requiresUpdate = $installed -and (-not $isUpToDate)
$requiresWarmup = $requiresUpdate -and $pluginChanged

$next = New-Object System.Collections.ArrayList
if (-not $installed) { [void]$next.Add("Agent not installed. Run install_agent_into_project.ps1 -TargetDir '$TargetDir' -AgentRoot '$SourceAgentRoot'.") }
elseif ($requiresUpdate) {
  [void]$next.Add("Run update_agent_in_project.ps1 -TargetDir '$TargetDir' -SourceAgentRoot '$SourceAgentRoot'" + $(if($ProjectUProject){" -ProjectUProject '$ProjectUProject'"}else{''}) + ".")
  if ($pluginChanged) { [void]$next.Add("Plugin source changed -> re-warmup after update (native_full not ready until then).") }
  if ($conflictCount -gt 0) { [void]$next.Add("$conflictCount managed file(s) modified in target; update backs them up before replacing.") }
  if ($editorRunning) { [void]$next.Add("UE editor is running; close it before a plugin re-warmup.") }
} else { [void]$next.Add("Up to date. No action needed.") }

$result = [ordered]@{
  schema_version='1.0'
  target_project=($TargetDir -replace '\\','/')
  project_uproject=($ProjectUProject -replace '\\','/')
  source_agent_root=($SourceAgentRoot -replace '\\','/')
  installed=$installed
  install_mode=$installMode
  current_version=$curVer
  source_version=$srcVerOut
  is_up_to_date=$isUpToDate
  requires_update=$requiresUpdate
  plugin_source_changed=$pluginChanged
  requires_warmup_after_update=$requiresWarmup
  editor_running=$editorRunning
  conflicts=@($conflicts)
  next_actions=@($next)
  generated_at=(Get-Date).ToUniversalTime().ToString('o')
}
Write-Utf8NoBom (Join-Path $outDir 'check_result.json') ($result | ConvertTo-Json -Depth 8)

$col = if (-not $installed) {'Yellow'} elseif ($isUpToDate) {'Green'} else {'Yellow'}
Write-Host ("check_project_agent_version: installed={0} up_to_date={1} requires_update={2} plugin_changed={3} conflicts={4}" -f `
  $installed,$isUpToDate,$requiresUpdate,$pluginChanged,$conflictCount) -ForegroundColor $col
Write-Host ("current={0}/{1}  source={2}/{3}  mode={4}" -f $curVer.agent_version,$curVer.agent_commit,$srcVerOut.agent_version,$srcVerOut.agent_commit,$installMode)
Write-Host ("check_result: {0}" -f (Join-Path $outDir 'check_result.json'))
$result | Write-Output

if (-not $installed) { exit 12 }
if ($requiresUpdate) { exit 10 }
exit 0
