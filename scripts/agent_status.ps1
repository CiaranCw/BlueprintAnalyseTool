<#
  agent_status.ps1 - READ-ONLY capability probe. Tells a calling AI, WITHOUT launching UE, which
  stage a project is at and which operations are available right now. Never modifies anything.

  Usage:
    .\agent_status.ps1 -ProjectUProject "<...>.uproject" [-UERoot "<engine>"] [-PluginName BPParserTestGen] [-OutputDir "<dir>"]

  Writes <OutputDir>/capability_state.json and prints a summary. Exit code always 0 (it's a probe).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $UERoot = "",
  [string] $PluginName = "BPParserTestGen",
  [string] $OutputDir = ""
)
$ErrorActionPreference='Continue'

function Resolve-UERoot([string]$uproject,[string]$override,[ref]$assocOut){
  $assoc = try { (Get-Content $uproject -Raw | ConvertFrom-Json).EngineAssociation } catch { "" }
  $assocOut.Value = $assoc
  if ($override) { return $override }
  if ($assoc -match '^\d+\.\d+$') {
    foreach ($rp in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$assoc","HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$assoc")) { try { $p=(Get-ItemProperty $rp -EA Stop).InstalledDirectory; if($p -and (Test-Path $p)){return $p} } catch {} }
    foreach ($d in 'C','D','E','F') { foreach ($b in @("$($d):\Program Files\Epic Games\UE_$assoc","$($d):\UE\UE_$assoc","$($d):\software\UE\UE_$assoc")) { if (Test-Path (Join-Path $b 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { return $b } } }
  } else {
    foreach ($rp in @("HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds","HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds")) { try { $v=(Get-ItemProperty $rp -EA Stop).$assoc; if($v -and (Test-Path $v)){return $v} } catch {} }
  }
  return ""
}

if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 0 }
$ProjectDir = Split-Path $ProjectUProject -Parent
$assoc = ""
$UERoot = Resolve-UERoot $ProjectUProject $UERoot ([ref]$assoc)

$cmdExe = if ($UERoot) { Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' } else { "" }
$cmdOk = [bool]($cmdExe -and (Test-Path $cmdExe))
$ver=""; $custom=$false
if ($UERoot) {
  $bv = Join-Path $UERoot 'Engine\Build\Build.version'
  if (Test-Path $bv) { try { $j=Get-Content $bv -Raw | ConvertFrom-Json; $ver="$($j.MajorVersion).$($j.MinorVersion).$($j.PatchVersion)" } catch {} }
  if ($assoc -notmatch '^\d+\.\d+$' -or -not (Test-Path (Join-Path $UERoot 'Engine\Build\InstalledBuild.txt'))) { $custom=$true }
}
$pythonOk = $false
if ($UERoot) { foreach ($pp in @('Engine\Plugins\Experimental\PythonScriptPlugin','Engine\Plugins\PythonScriptPlugin')) { if (Test-Path (Join-Path $UERoot $pp)) { $pythonOk=$true } } }

$pluginDir = Join-Path $ProjectDir "Plugins\$PluginName"
$pluginInstalled = Test-Path (Join-Path $pluginDir "$PluginName.uplugin")
$pluginBuilt = $false
if ($pluginInstalled) { $pluginBuilt = [bool](Get-ChildItem (Join-Path $pluginDir 'Binaries\Win64') -Filter "UnrealEditor-$PluginName.dll" -EA SilentlyContinue) }

$modes = @('offline_asset_scan')
if ($cmdOk -and $pythonOk) { $modes += 'python_partial' }
$nativeReady = ($cmdOk -and $pluginInstalled -and $pluginBuilt)
if ($nativeReady) { $modes += 'native_full' }

# stage machine
$stage = if (-not $cmdOk) { 'offline_only' }
  elseif ($nativeReady) { 'native_ready' }
  elseif ($pluginInstalled -and -not $pluginBuilt) { 'needs_build' }
  elseif (-not $pluginInstalled) { 'needs_install' }
  else { 'python_only' }

$caps = [ordered]@{
  understand_partial = ($modes -contains 'python_partial' -or $modes -contains 'offline_asset_scan')
  understand_full    = $nativeReady
  edit               = $nativeReady
  create             = $nativeReady
}
$warmupRequired = -not $nativeReady

$rec = switch ($stage) {
  'native_ready' { 'Ready. Call analyze/edit/create with Mode=native_full (or auto).' }
  'needs_build'  { 'Plugin installed but not built. Run warmup_project.ps1 (or build_project_plugin.ps1) to enable native_full.' }
  'needs_install'{ 'Run warmup_project.ps1 (install + build) to enable full understanding/edit/create. Until then: python_partial/offline only.' }
  'python_only'  { 'No native plugin. Use python_partial/offline for partial understanding; run warmup_project.ps1 for full/edit/create.' }
  default        { 'UE engine not resolvable. Only offline_asset_scan is possible; pass -UERoot or fix EngineAssociation.' }
}
$nextCalls = @()
if ($warmupRequired -and $cmdOk) { $nextCalls += ".\scripts\warmup_project.ps1 -ProjectUProject `"$ProjectUProject`" -UERoot `"$UERoot`"   # one-time; needs consent (adds plugin + incremental build)" }
$nextCalls += ".\scripts\blueprint_agent.ps1 -RequestJson <request.json>   # task_type analyze|edit|create; Mode auto self-selects the best available"

if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports\status' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$state = [ordered]@{
  schema_version='1.0'; probed_at=(Get-Date).ToUniversalTime().ToString('o'); read_only=$true
  project_uproject=$ProjectUProject; engine_association=$assoc; ue_root=$UERoot
  engine_version=$ver; is_custom_engine=$custom
  unreal_cmd_present=$cmdOk; python_plugin_available=$pythonOk
  plugin_name=$PluginName; plugin_installed=$pluginInstalled; plugin_built=$pluginBuilt
  stage=$stage; warmup_required=$warmupRequired
  available_modes=$modes; capabilities=$caps
  recommended_action=$rec; next_calls=$nextCalls
}
$stateFile = Join-Path $OutputDir 'capability_state.json'
[IO.File]::WriteAllText($stateFile, ($state | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))  # no-BOM UTF-8 for direct json.load

Write-Host "== agent_status ==" -ForegroundColor Cyan
Write-Host "stage=$stage  engine=$ver$(if($custom){' (custom)'})  cmd=$cmdOk python=$pythonOk plugin_installed=$pluginInstalled plugin_built=$pluginBuilt"
Write-Host "available_modes: $($modes -join ', ')"
Write-Host "capabilities: understand_full=$($caps.understand_full) edit=$($caps.edit) create=$($caps.create)  warmup_required=$warmupRequired"
Write-Host "-> $rec" -ForegroundColor Yellow
Write-Host "state: $stateFile"
exit 0
